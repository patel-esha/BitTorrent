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
#include "peer.h"

std::atomic<bool> running{true};
constexpr int BUFFER_SIZE = 1024;

Peer::Peer(int id) : peerId(id), logger(*this) {
    loadCommonConfig("../Common.cfg");
    loadPeerInfo("../PeerInfo.cfg");

    int numPieces = (size + pieceSize - 1) / pieceSize;
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
    std::thread listener(&Peer::listenForPeers, this);
    connectToPeers();
    listener.join();
}

int Peer::loadPeerInfo(const std::string& fileName) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        std::cerr << "Error: could not open " << fileName << std::endl;
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

int Peer::loadCommonConfig(const std::string& fileName) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open Common.cfg" << std::endl;
        return 1;
    }

    std::string key;
    file >> key >> numPreferredNeighbors;
    file >> key >> unchokingInterval;
    file >> key >> optimisticUnchokingInterval;
    file >> key >> name;
    file >> key >> size;
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
            threads.emplace_back(&Peer::handleConnection, this, clientSocket);
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
            inet_pton(AF_INET, "192.168.0.7", &serverAddr.sin_addr);

            if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                std::cerr << "Connection failed.\n";
                return 1;
            }

            // send handshake
            auto handshake = createHandshake();
            send(sock, handshake.data(), handshake.size(), 0);

            // handle connection in a new thread
            std::thread(&Peer::handleConnection, this, sock).detach();
        }
    }
    return 0;
}

void Peer::handleConnection(int socket) {
    int remotePeerID = -1;

    // receive handshake from remote peer
    if (!receiveHandshake(socket, remotePeerID)) {
        close(socket);
        return;
    }

    peerSockets[remotePeerID] = socket;

    std::cout << "Peer " << peerId << " connected with Peer " << remotePeerID << std::endl;

    // send handshake back
    auto handshake = createHandshake();
    send(socket, handshake.data(), handshake.size(), 0);

    // send bitfield
    sendBitfield(socket);

    //enter message loop
    while (running) {
        Message msg;
        if (!receiveMessage(socket, msg)) break;

        handleMessage(remotePeerID, msg);
    }

    // cleanup
    //peerSockets.erase(remotePeerID);
    //close(socket);
}

std::vector<unsigned char> Peer::createHandshake() {
    std::vector<unsigned char> msg(32,0);

    const char* header = "P2PFILESHARINGPROJ";
    std::memcpy(msg.data(), header, 18);

    int32_t idN = htonl(peerId);
    memcpy(msg.data() + 28, &idN, 4);

    return msg;
}

bool Peer::receiveHandshake(int socket, int &remotePeerID) {
    unsigned char hs[32];

    ssize_t bytes = recv(socket, hs, 32, MSG_WAITALL);
    if (bytes != 32)
        return false;

    int32_t id;
    memcpy(&id, hs + 28, sizeof(id));
    remotePeerID = ntohl(id);
    std::cout << "Peer " << peerId << " received handshake from Peer " << remotePeerID << std::endl;

    return true;
}

void Peer::sendBitfield(int socket) {
    // Format: length (4 bytes), type = 5, payload = bitfield
    uint32_t length = htonl(1 + bitfield.size());
    unsigned char type = 5;  // bitfield message type

    std::vector<unsigned char> msg(4 + 1 + bitfield.size());
    memcpy(msg.data(), &length, 4);
    msg[4] = type;

    // convert vector<bool> to bytes
    for (size_t i = 0; i < bitfield.size(); i++)
        msg[5 + i] = bitfield[i] ? 1 : 0;

    send(socket, msg.data(), msg.size(), 0);

    std::cout << "Peer " << peerId << " sent bitfield" << std::endl;
}

bool Peer::receiveMessage(int socket, Message &msg) {
    // Format: length (4 bytes), type = 5, payload = bitfield
    uint32_t lenNet;
    ssize_t bytes = recv(socket, &lenNet, 4, MSG_WAITALL);
    if (bytes <= 0) return false;

    msg.length = ntohl(lenNet);

    // read type
    bytes = recv(socket, &msg.type, 1, MSG_WAITALL);
    if (bytes <= 0) return false;

    int payloadLen = msg.length - 1;

    msg.payload.resize(payloadLen);
    if (payloadLen > 0) {
        bytes = recv(socket, msg.payload.data(), payloadLen, MSG_WAITALL);
        if (bytes <= 0) return false;
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


    bitfield[idx] = true;

    // Remove from requested set
    {
        std::lock_guard<std::mutex> lock(requestedPiecesMutex);
        requestedPieces.erase(idx);
    }


    logger.logDownloadingPiece(remoteID, idx, countPiecesOwned());

    // Broadcast HAVE message to all
    broadcastHave(idx);

            // Check if download is complete
    if (hasCompletedDownload())
    {
        logger.logDownloadComplete();
        std::cout << "Peer " << peerId << " has completed download!" << std::endl;
        // TODO: Check if ALL peers are done (termination condition)
    }

    // Request next piece from this peer
    // CHECK FOR:
    requestNextPiece(remoteID);
}

void Peer::handleHave(int remoteID, const std::vector<unsigned char> &payload) {
    int32_t pieceIndex;
    memcpy(&pieceIndex, payload.data(), 4);
    pieceIndex = ntohl(pieceIndex);

    logger.logReceivingHave(remoteID, pieceIndex);

    // Update their bitfield
    if (neighborBitfields.count(remoteID)) {
        neighborBitfields[remoteID][pieceIndex] = true;}

    // If we need this piece, send interested
    if (!bitfield[pieceIndex]) {
        sendMessage(peerSockets[remoteID], 2, {}); // interested
    }
}
void Peer::handleBitfield(int remoteID, const std::vector<unsigned char> &payload) {
    // Store their bitfield
    std::vector<bool> remoteBitfield(bitfield.size());
    for (size_t i = 0; i < payload.size() && i < bitfield.size(); i++) {
        remoteBitfield[i] = (payload[i] == 1);
    }
    neighborBitfields[remoteID] = remoteBitfield;


    // Check if they have anything we need
    bool interested = false;
    for (size_t i = 0; i < bitfield.size(); i++) {
        if (!bitfield[i] && remoteBitfield[i]) {
            interested = true;
            break;
        }
    }

    // Send interested/not interested
    unsigned char msgType = interested ? 2 : 3;
    sendMessage(peerSockets[remoteID], msgType, {});
}
std::string Peer::getPieceFilePath(int pieceIndex) {
    std::string dirPath = "peer_" + std::to_string(peerId);
    return dirPath + "/" + name;  // name is from Common.cfg
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
    int numPieces = (size + pieceSize - 1) / pieceSize;
    int currentPieceSize = pieceSize;
    if (pieceIndex == numPieces - 1) {
        // Last piece
        currentPieceSize = size - (pieceIndex * pieceSize);
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
    // Create HAVE message payload (4byte index)
    std::vector<unsigned char> payload(4);
    int32_t idxNet = htonl(pieceIndex);
    memcpy(payload.data(), &idxNet, 4);

    // Send to all
    for (auto& [remotePeerID, socket] : peerSockets) {
        sendMessage(socket, 4, payload);  // type 4 = have
    }

    std::cout << "Peer " << peerId << " broadcasted HAVE for piece " << pieceIndex << " to all peers" << std::endl;
}
