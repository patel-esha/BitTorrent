#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <csignal>
#include <atomic>
#include <arpa/inet.h>
#include <fstream>
#include <algorithm>
#include "peer.h"

std::atomic<bool> running{true};
constexpr int BUFFER_SIZE = 1024;

Peer::Peer(int id) : peerId(id), logger(*this) {
    loadCommonConfig("../Common.cfg");
    loadPeerInfo("../PeerInfo.cfg");

    numPieces = (fileSize + pieceSize - 1) / pieceSize;
    bitfield.resize(numPieces);
    if (self.hasFile) {
        std::fill(bitfield.begin(), bitfield.end(), true);
    } else {
        std::fill(bitfield.begin(), bitfield.end(), false);
    }
}

int Peer::getPeerId() {
    return peerId;
}

void Peer::start() {
    srand(time(nullptr) + peerId);  // seed random for piece selection

    std::thread listener(&Peer::listenForPeers, this);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // give listener time to start

    connectToPeers();

    // Start the timer threads for choking/unchoking
    std::thread prefTimer(&Peer::preferredNeighborTimer, this);
    std::thread optTimer(&Peer::optimisticUnchokeTimer, this);

    // Wait for threads to finish
    if (prefTimer.joinable()) prefTimer.join();
    if (optTimer.joinable()) optTimer.join();
    if (listener.joinable()) listener.join();
}

int Peer::loadPeerInfo(const std::string& peerFile) {
    std::ifstream file(peerFile);
    if (!file.is_open()) {
        std::cerr << "Error: could not open " << peerFile << std::endl;
        return 1;
    }

    peers.clear();
    int id = 0;
    int port = 0;
    bool hasFile = false;
    std::string host;

    while (file >> id >> host >> port >> hasFile) {
        PeerInfo info;
        info.id = id;
        info.hostName = "localhost";
        info.port = port;
        info.hasFile = hasFile;
        peers.push_back(info);

        if (id == this->peerId) {
            self = info;
        }
    }

    file.close();
    return 0;
}

int Peer::loadCommonConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open Common.cfg" << std::endl;
        return 1;
    }

    std::string key;
    file >> key >> numPreferredNeighbors;
    file >> key >> unchokingInterval;
    file >> key >> optimisticUnchokingInterval;
    file >> key >> fileName;
    file >> key >> fileSize;
    file >> key >> pieceSize;

    file.close();
    return 0;
}

int Peer::listenForPeers() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(self.port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(serverSocket, 5) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Peer " << peerId << " listening on port " << self.port << "...\n";

    std::vector<std::thread> threads;

    while (running) {
        sockaddr_in clientAddr{};
        socklen_t clientSize = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientSize);

        if (clientSocket >= 0) {
            threads.emplace_back(&Peer::handleConnection, this, clientSocket, false);
        }
    }

    for (auto& t : threads)
        if (t.joinable()) t.join();

    close(serverSocket);
    return 0;
}

int Peer::connectToPeers() {
    for (auto& peerInfo : peers) {
        if (peerInfo.id < this->peerId) {  // connect only to earlier peers
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
                std::cerr << "Failed to create socket.\n";
                return 1;
            }

            sockaddr_in serverAddr{};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(peerInfo.port);
            // change to local IP for local testing
            inet_pton(AF_INET, "192.168.0.42", &serverAddr.sin_addr);

            if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                std::cerr << "Connection failed.\n";
                return 1;
            }

            // send handshake
            sendHandshake(sock);
            // handle connection in a new thread
            std::thread(&Peer::handleConnection, this, sock, true).detach();
        }
    }
    return 0;
}

void Peer::handleConnection(int sock, bool isInitiator) {
    int remoteID = -1;

    if (isInitiator) {
        // Already sent handshake in connectToPeers
        if (!receiveHandshake(sock, remoteID)) { close(sock); return; }
    } else {
        if (!receiveHandshake(sock, remoteID)) { close(sock); return; }
        sendHandshake(sock);
    }

    {
        std::lock_guard<std::mutex> lg(socketMutex);
        peerSockets[remoteID] = sock;
    }

    sendBitfield(sock);

    while (running) {
        Message msg;
        if (!receiveMessage(sock, msg)) break;
        handleMessage(remoteID, msg);
    }
}



void Peer::sendHandshake(int socket) {
    std::vector<unsigned char> msg(32,0);

    const char* header = "P2PFILESHARINGPROJ";
    std::memcpy(msg.data(), header, 18);

    int32_t idN = htonl(peerId);
    memcpy(msg.data() + 28, &idN, 4);

    send(socket, msg.data(), msg.size(), 0);
}

bool Peer::receiveHandshake(int socket, int &remotePeerID) {
    unsigned char hs[32];

    ssize_t bytes = readNBytes(socket, hs, 32);
    if (bytes != 32)
        return false;

    int32_t id;
    memcpy(&id, hs + 28, sizeof(id));
    remotePeerID = ntohl(id);
    std::cout << "Peer " << peerId << " received handshake from Peer " << remotePeerID << std::endl;

    return true;
}

void Peer::sendBitfield(int socket) {
    auto payload = bitfieldToBytes();
    sendMessage(socket, 5, payload); // type 5 == bitfield
    std::cout << "Peer " << peerId << " sent bitfield" << std::endl;
}

bool Peer::receiveMessage(int socket, Message &msg) {
    // Format: length (4 bytes), type = 5, payload = bitfield
    uint32_t lenNet;
    if (readNBytes(socket, &lenNet, 4) <= 0) return false;

    msg.length = ntohl(lenNet);

    // read type
    if (readNBytes(socket, &msg.type, 1) <= 0) return false;

    int payloadLen = msg.length - 1;

    msg.payload.resize(payloadLen);
    if (payloadLen > 0) {
        if (readNBytes(socket, msg.payload.data(), payloadLen) <= 0) return false;
    }

    return true;
}

bool Peer::sendMessage(int socket, unsigned char type, const std::vector<unsigned char> &payload) {
    uint32_t length = 1 + payload.size();
    uint32_t lenNet = htonl(length);

    // full message buffer
    std::vector<unsigned char> buf(4 + length);
    memcpy(buf.data(), &lenNet, 4);
    buf[4] = type;

    memcpy(buf.data() + 5, payload.data(), payload.size());

    ssize_t sent = send(socket, buf.data(), buf.size(), 0);
    return sent == static_cast<ssize_t>(buf.size());
}

void Peer::handleMessage(int remoteID, const Message &msg) {
    switch (msg.type) {
        case 0: handleChoke(remoteID); break;
        case 1:  handleUnchoke(remoteID); break;
        case 2:  handleInterested(remoteID); break;
        case 3:  handleNotInterested(remoteID); break;
        case 4:  handleHave(remoteID, msg.payload); break;
        case 5:  handleBitfield(remoteID, msg.payload); break;
        case 6:  handleRequest(remoteID, msg.payload); break;
        case 7:  handlePiece(remoteID, msg.payload); break;
        default:
            std::cerr << "Unknown message type " << (int)msg.type << "\n";
    }
}

void Peer::handleInterested(int remoteID) {
    neighborStates[remoteID].peerInterested = true;
    logger.logReceivingInterested(remoteID);
}

void Peer::handleNotInterested(int remoteID) {
    neighborStates[remoteID].peerInterested = false;
    logger.logReceivingNotInterested(remoteID);
}

void Peer::handleChoke(int remoteID) {
    neighborStates[remoteID].peerChoking = true;
    logger.logChoking(remoteID);
}

//this function was pretty much done idk why there was a TODO here but mby im missing something
void Peer::handleUnchoke(int remoteID) {
    neighborStates[remoteID].peerChoking = false;
    logger.logUnchoking(remoteID);

    std::cout << "Peer " << peerId << " was unchoked by peer " << remoteID << std::endl;

    //now we can request a piece
    requestNextPiece(remoteID);
}

void Peer::handleRequest(int remoteID, const std::vector<unsigned char>& payload) {
    int32_t idx;
    memcpy(&idx, payload.data(), 4);
    idx = ntohl(idx);

    std::cout << "Peer " << peerId << " received REQUEST for piece " << idx << " from peer " << remoteID << std::endl;

    // chewck if this peer is unchoked
    if (neighborStates[remoteID].amChoking) {
        std::cout << "Peer " << peerId << " ignoring request from choked peer "<< remoteID << std::endl;
        return;
    }

    sendPiece(remoteID, idx);
}

void Peer::handlePiece(int remoteID, const std::vector<unsigned char>& payload) {
    int32_t idx;
    memcpy(&idx, payload.data(), 4);
    idx = ntohl(idx);
    //same as before

    std::vector<unsigned char> data(payload.begin() + 4, payload.end());

    std::cout << "Peer " << peerId << " received piece " << idx
              << " from peer " << remoteID << " (" << data.size()
              << " bytes)" << std::endl;


    savePiece(idx, data);
    updateMyBitfield(idx);

    // Remove from requested set
    {
        std::lock_guard<std::mutex> lock(requestedPiecesMutex);
        requestedPieces.erase(idx);
    }

    logger.logDownloadingPiece(remoteID, idx, countPiecesOwned());
    requestNextPiece(remoteID);
}

void Peer::handleHave(int remoteID, const std::vector<unsigned char>& payload) {
    if (payload.size() < 4) return; // malformed

    int32_t idxNet;
    memcpy(&idxNet, payload.data(), 4);
    int pieceIndex = ntohl(idxNet);

    logger.logReceivingHave(remoteID, pieceIndex);

    // bounds check
    if (pieceIndex < 0 || pieceIndex >= (int)bitfield.size()) {
        std::cerr << "Received HAVE for invalid piece index";
        return;
    }

    // update neighbor bitfield
    {
        std::lock_guard<std::mutex> lg(neighborMutex);
        auto& bf = neighborBitfields[remoteID];
        if ((int)bf.size() < (int)bitfield.size()) {
            bf.resize(bitfield.size(), false);
        }
        bf[pieceIndex] = true;
    }

    // recalc whether we are interested
    bool wasInterested = neighborStates[remoteID].amInterested;
    bool isNowInterested = peerHasInterestingPieces(remoteID);

    if (isNowInterested && !wasInterested) {
        sendInterested(remoteID);
        neighborStates[remoteID].amInterested = true;

        if (!neighborStates[remoteID].peerChoking) {
            requestNextPiece(remoteID);
        }
    }

    if (hasCompletedDownload() && allPeersComplete()) {
        std::cout << "All peers have complete file. Terminating..." << std::endl;
        running = false;
    }
}

void Peer::handleBitfield(int remoteID, const std::vector<unsigned char> &payload) {
    // Store their bitfield
    std::vector<bool> remoteBitfield = bytesToBitfield(payload, bitfield.size());

    {
        std::lock_guard<std::mutex> lg(neighborMutex);
        neighborBitfields[remoteID] = remoteBitfield;
    }

    // Check if they have anything we need
    bool interested = false;
    for (size_t i = 0; i < bitfield.size(); i++) {
        if (!bitfield[i] && remoteBitfield[i]) {
            interested = true;
            break;
        }
    }

    std::cout << "Peer " << peerId << " parsed remote bitfield from "
              << remoteID << ": ";
    for (bool b : remoteBitfield) std::cout << b;
    std::cout << std::endl;

    // Send interested/not interested using socket mutex
    if (interested) {
        sendInterested(remoteID);
        neighborStates[remoteID].amInterested = true;
        if (!neighborStates[remoteID].peerChoking) {
            std::cout << "Peer " << peerId << " strarting requests from " << remoteID
                      << remoteID << std::endl;
            requestNextPiece(remoteID);
        }
    } else {
        sendNotInterested(remoteID);
        neighborStates[remoteID].amInterested = false;
    }

    if (hasCompletedDownload() && allPeersComplete()) {
        std::cout << "All peers have complete file. Terminating..." << std::endl;
        running = false;
    }
}

std::string Peer::getPieceFilePath(int pieceIndex) {
    std::string dirPath = "../peer_" + std::to_string(peerId);
    return dirPath + "/" + fileName;  // name is from Common.cfg
}
void Peer::savePiece(int pieceIndex, const std::vector<unsigned char>& data) {
    std::string filePath = getPieceFilePath(pieceIndex);

    // Open file in binary mode for read/write, creates it if doesn't exist
    std::fstream file(filePath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        // File doesn't exist, create it
        file.open(filePath, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot create file " << filePath << std::endl;
            return;
        }
        file.close();
        file.open(filePath, std::ios::binary | std::ios::in | std::ios::out);
    }

    // Calculate offset in file
    std::streampos offset = static_cast<std::streampos>(pieceIndex) * pieceSize;

    // Seek to position and write
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();

    std::cout << "Peer " << peerId << " saved piece " << pieceIndex
              << " (" << data.size() << " bytes)" << std::endl;
}
std::vector<unsigned char> Peer::loadPiece(int pieceIndex) {
    std::string filePath = getPieceFilePath(pieceIndex);

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filePath << std::endl;
        return {};
    }

    // Calculate offset and size
    std::streampos offset = static_cast<std::streampos>(pieceIndex) * pieceSize;

    // Calculate piece size (last piece might be smaller)
    int currentPieceSize = pieceSize;
    if (pieceIndex == numPieces - 1) {
        // Last piece
        currentPieceSize = fileSize - (pieceIndex * pieceSize);
    }

    // Seek and read
    file.seekg(offset);
    std::vector<unsigned char> data(currentPieceSize);
    file.read(reinterpret_cast<char*>(data.data()), currentPieceSize);
    file.close();

    std::cout << "Peer " << peerId << " loaded piece " << pieceIndex
              << " (" << data.size() << " bytes)" << std::endl;

    return data;
}
int Peer::selectRandomPiece(int remoteID) {
    //check if we have neighbor bitfield
    if (neighborBitfields.find(remoteID) == neighborBitfields.end()) {
        return -1;  // Don't know what they have
    }

    std::vector<bool>& remoteBitfield = neighborBitfields[remoteID];

    // Find pieces that:
    // 1.remote peer has
    // 2.We dont have
    // 3.we havent requested yet
    std::vector<int> availablePieces;

    std::lock_guard<std::mutex> lock(requestedPiecesMutex);

    for (size_t i = 0; i < bitfield.size(); i++) {
        if (!bitfield[i] &&                           // We dont have it
            remoteBitfield[i] &&                      // They have it
            requestedPieces.find(i) == requestedPieces.end()) // Not requested
        {
            availablePieces.push_back(i);
        }
    }

    if (availablePieces.empty()) {
        return -1;  // No pieces available
    }

    // Random selection
    int randomIndex = rand() % availablePieces.size();
    int selectedPiece = availablePieces[randomIndex];

    // Mark as requested
    requestedPieces.insert(selectedPiece);

    std::cout << "Peer " << peerId << " selected piece " << selectedPiece
              << " from peer " << remoteID << std::endl;

    return selectedPiece;
}

int Peer::countPiecesOwned() {
    int count = 0;
    for (bool hasPiece : bitfield) {
        if (hasPiece) count++;
    }
    return count;
}

bool Peer::hasCompletedDownload() {
    for (bool hasPiece : bitfield) {
        if (!hasPiece) return false;
    }
    return true;
}
void Peer::requestNextPiece(int remoteID) {
    // Check if we're choked
    if (neighborStates[remoteID].peerChoking) {
        std::cout << "Peer " << peerId << " is choked by peer " << remoteID
                  << ", cannot request" << std::endl;
        return;
    }

    // random
    int pieceIndex = selectRandomPiece(remoteID);

    if (pieceIndex == -1) {
        std::cout << "Peer " << peerId << " has no pieces to request from peer "
                  << remoteID << std::endl;

        // Send not interested
        sendMessage(peerSockets[remoteID], 3, {});  // type 3 = not interested
        return;
    }

    // Create REQUEST message payload (4-byte piece index ad per the pdf)
    std::vector<unsigned char> payload(4);
    int32_t idxNet = htonl(pieceIndex);
    //I hate c++
    memcpy(payload.data(), &idxNet, 4);

    // Send request message = type 6
    sendMessage(peerSockets[remoteID], 6, payload);

    std::cout << "Peer " << peerId << " requested piece " << pieceIndex
              << " from peer " << remoteID << std::endl;

}
void Peer::sendPiece(int remoteID, int pieceIndex) {
        // check if we have this piece in the first place
    if (!bitfield[pieceIndex]) {
        std::cerr << "Error: Peer " << peerId << " doesn't have piece " << pieceIndex << std::endl;
        return;
    }

    // Load
    std::vector<unsigned char> pieceData = loadPiece(pieceIndex);

    if (pieceData.empty()) {
        std::cerr << "Error: Failed to load piece " << pieceIndex << std::endl;
        return;
    }

    // Create piece message payload: 4-byte index + piece data
    std::vector<unsigned char> payload(4 + pieceData.size());
    int32_t idxNet = htonl(pieceIndex);
    memcpy(payload.data(), &idxNet, 4);
    memcpy(payload.data() + 4, pieceData.data(), pieceData.size());

    // Send PIECE message (type 7)
    sendMessage(peerSockets[remoteID], 7, payload);

    std::cout << "Peer " << peerId << " sent piece " << pieceIndex
              << " to peer " << remoteID << " (" << pieceData.size()
              << " bytes)" << std::endl;
    //more debuging
}

void Peer::broadcastHave(int pieceIndex) {
    if (pieceIndex < 0 || pieceIndex >= (int)bitfield.size())
        return;

    std::vector<unsigned char> payload(4);
    int32_t idxNet = htonl(pieceIndex);
    memcpy(payload.data(), &idxNet, 4);

    std::vector<int> sockets;

    {
        std::lock_guard<std::mutex> lg(socketMutex);
        for (auto& [remotePeerID, socket] : peerSockets) {
            sockets.push_back(socket);
        }
    }

    for (int sock : sockets) {
        sendMessage(sock, 4, payload);
    }

    // TODO: add this in logger
    //logger.logBroadcastHave("Peer " + std::to_string(peerId) +
    //           " broadcasted HAVE for piece " + std::to_string(pieceIndex));
}


// convert my vector<bool> bitfield to bytes for message payload
std::vector<unsigned char> Peer::bitfieldToBytes() {
    std::lock_guard<std::mutex> lg(bitfieldMutex);
    int nbits = (int)bitfield.size();
    int nbytes = (nbits + 7) / 8;
    std::vector<unsigned char> out(nbytes, 0);

    for (int i = 0; i < nbits; ++i) {
        if (bitfield[i]) {
            int byteIndex = i / 8;
            int bitIndex = 7 - (i % 8); // highest bit = piece index 0 in that byte
            out[byteIndex] |= (1 << bitIndex);
        }
    }
    return out;
}

// parse byte payload into vector<bool> with expectedBits length
std::vector<bool> Peer::bytesToBitfield(const std::vector<unsigned char>& payload, int expectedBits) {
    std::vector<bool> out(expectedBits, false);
    int nbytes = payload.size();
    for (int b = 0; b < nbytes; ++b) {
        unsigned char byte = payload[b];
        for (int bit = 0; bit < 8; ++bit) {
            int pieceIndex = b * 8 + bit;
            if (pieceIndex >= expectedBits) break;
            int bitPos = 7 - bit; // map high->low to indices
            out[pieceIndex] = ((byte >> bitPos) & 0x1);
        }
    }
    return out;
}

bool Peer::peerHasInterestingPieces(int remoteID) {
    std::lock_guard<std::mutex> lg1(bitfieldMutex);
    std::lock_guard<std::mutex> lg2(neighborMutex);

    auto it = neighborBitfields.find(remoteID);
    if (it == neighborBitfields.end()) return false;

    const std::vector<bool>& neighborBF = it->second;
    int n = std::min((int)bitfield.size(), (int)neighborBF.size());
    for (int i = 0; i < n; ++i) {
        if (neighborBF[i] && !bitfield[i]) return true; // they have something I don't
    }
    return false;
}

void Peer::updateMyBitfield(int pieceIndex) {
    {
        std::lock_guard<std::mutex> lg(bitfieldMutex);
        if (pieceIndex < 0 || pieceIndex >= (int)bitfield.size()) return;
        if (bitfield[pieceIndex]) return; // already set
        bitfield[pieceIndex] = true;
    }

    std::cout << "Peer " << peerId << " completed piece " << pieceIndex
              << " (" << countPiecesOwned() << "/" << numPieces << ")" << std::endl;

    broadcastHave(pieceIndex);

    std::vector<int> peerIDs;
    {
        std::lock_guard<std::mutex> lg(socketMutex);
        for (auto& [id, sock] : peerSockets) {
            peerIDs.push_back(id);
        }
    }

    for (int remoteID : peerIDs) {
        bool wasInterested = neighborStates[remoteID].amInterested;
        bool isInteresting = peerHasInterestingPieces(remoteID);

        if (!isInteresting && wasInterested) {
            sendNotInterested(remoteID);
            neighborStates[remoteID].amInterested = false;
            std::cout << "Peer " << peerId << " no longer interested in "
                      << remoteID << std::endl;
        }
    }

    // Check download completion
    if (hasCompletedDownload()) {
        std::cout << "Peer " << peerId << " has downloaded the complete file!"
                  << std::endl;

        if (allPeersComplete()) {
            std::cout << "All peers have complete file. Terminating..." << std::endl;
            running = false;
        }
    }
}

void Peer::sendInterested(int remoteID) {
    int sock = -1;
    {
        std::lock_guard<std::mutex> lg(socketMutex);
        auto it = peerSockets.find(remoteID);
        if (it == peerSockets.end()) return;
        sock = it->second;
    }
    sendMessage(sock, 2, {}); // type 2 == interested

    // TODO: add this logger
    //logger.log("Peer " + std::to_string(peerId) + " sent 'interested' to " + std::to_string(remoteID));
    std::cout << "sent interested" << std::endl;
}

void Peer::sendNotInterested(int remoteID) {
    int sock = -1;
    {
        std::lock_guard<std::mutex> lg(socketMutex);
        auto it = peerSockets.find(remoteID);
        if (it == peerSockets.end()) return;
        sock = it->second;
    }
    sendMessage(sock, 3, {}); // type 3 == not interested

    // TODO: add this logger
    //logger.log("Peer " + std::to_string(peerId) + " sent 'not interested' to " + std::to_string(remoteID));
    std::cout << "sent not interested" << std::endl;
}

ssize_t Peer::readNBytes(int sock, void* buffer, size_t n) {
    size_t total = 0;
    char* ptr = (char*)buffer;
    while (total < n) {
        ssize_t r = recv(sock, ptr + total, n - total, 0);
        if (r <= 0) return r; // error or closed
        total += r;
    }
    return total;
}
// choke/unchoke - esha
void Peer::selectPreferredNeighbors() {
    std::lock_guard<std::mutex> lock(neighborMutex);

    std::vector<std::pair<int, double>> candidates;

    for (auto& [peerID, state] : neighborStates) {
        if (state.peerInterested) {
            candidates.push_back({peerID, state.downloadRate});
        }
    }

    if (hasCompletedDownload()) {
        std::random_shuffle(candidates.begin(), candidates.end());
    } else {
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    std::vector<int> newPreferredNeighbors;
    int count = std::min((int)candidates.size(), numPreferredNeighbors);
    for (int i = 0; i < count; i++) {
        newPreferredNeighbors.push_back(candidates[i].first);
    }

    if (!newPreferredNeighbors.empty()) {
        logger.logPreferredNeighborsChange(newPreferredNeighbors);
    }

    for (int peerID : newPreferredNeighbors) {
        if (neighborStates[peerID].amChoking) {
            neighborStates[peerID].amChoking = false;
            int sock;
            {
                std::lock_guard<std::mutex> lg(socketMutex);
                sock = peerSockets[peerID];
            }
            sendMessage(sock, 1, {}); // unchoke
            std::cout << "Peer " << peerId << " sent UNCHOKE to peer " << peerID << std::endl;
        }
    }

    for (auto& [peerID, state] : neighborStates) {
        bool isPreferred = std::find(newPreferredNeighbors.begin(), newPreferredNeighbors.end(), peerID) != newPreferredNeighbors.end();
        bool isOptimistic = (peerID == optimisticallyUnchokedNeighbor);

        if (!isPreferred && !isOptimistic && !state.amChoking) {
            state.amChoking = true;
            int sock;
            {
                std::lock_guard<std::mutex> lg(socketMutex);
                sock = peerSockets[peerID];
            }
            sendMessage(sock, 0, {}); // choke
            std::cout << "Peer " << peerId << " sent CHOKE to peer " << peerID << std::endl;
        }
    }

    for (auto& [peerID, state] : neighborStates) {
        state.downloadRate = 0.0;
        state.bytesDownloaded = 0;
    }
}

void Peer::selectOptimisticallyUnchokedNeighbor() {
    std::lock_guard<std::mutex> lock(neighborMutex);

    std::vector<int> candidates;
    for (auto& [peerID, state] : neighborStates) {
        if (state.amChoking && state.peerInterested) {
            candidates.push_back(peerID);
        }
    }

    if (candidates.empty()) {
        return;
    }

    int randomIndex = rand() % candidates.size();
    int selectedPeer = candidates[randomIndex];

    if (optimisticallyUnchokedNeighbor != -1 && optimisticallyUnchokedNeighbor != selectedPeer) {
        auto& oldState = neighborStates[optimisticallyUnchokedNeighbor];
        if (!oldState.amChoking) {
            oldState.amChoking = true;
            int sock;
            {
                std::lock_guard<std::mutex> lg(socketMutex);
                sock = peerSockets[optimisticallyUnchokedNeighbor];
            }
            sendMessage(sock, 0, {}); // choke
            std::cout << "Peer " << peerId << " sent CHOKE to peer " << optimisticallyUnchokedNeighbor << std::endl;
        }
    }

    optimisticallyUnchokedNeighbor = selectedPeer;
    logger.logOptimisticallyUnchokedNeighbor(selectedPeer);

    neighborStates[selectedPeer].amChoking = false;
    int sock;
    {
        std::lock_guard<std::mutex> lg(socketMutex);
        sock = peerSockets[selectedPeer];
    }
    sendMessage(sock, 1, {}); // unchoke
    std::cout << "Peer " << peerId << " sent UNCHOKE to peer " << selectedPeer << std::endl;
}

void Peer::preferredNeighborTimer() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(unchokingInterval));
        if (!running) break;
        selectPreferredNeighbors();
    }
}

void Peer::optimisticUnchokeTimer() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(optimisticUnchokingInterval));
        if (!running) break;
        selectOptimisticallyUnchokedNeighbor();
    }
}

void Peer::updateDownloadRate(int remoteID, size_t bytes) {
    std::lock_guard<std::mutex> lock(neighborMutex);
    neighborStates[remoteID].bytesDownloaded += bytes;
    neighborStates[remoteID].downloadRate =
        neighborStates[remoteID].bytesDownloaded / (double)unchokingInterval;
}

bool Peer::allPeersComplete() {
    if (!hasCompletedDownload()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(neighborMutex);
    for (auto& [peerID, bitfield] : neighborBitfields) {
        for (bool hasPiece : bitfield) {
            if (!hasPiece) {
                return false;
            }
        }
    }

    return true;
}