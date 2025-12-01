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

void Peer::handleUnchoke(int remoteID) {
    neighborStates[remoteID].peerChoking = false;
    logger.logUnchoking(remoteID);

    // TODO: implement this function
    //requestNextPiece(remoteID);
}

void Peer::handleRequest(int remoteID, const std::vector<unsigned char>& payload) {
    int32_t idx;
    memcpy(&idx, payload.data(), 4);
    idx = ntohl(idx);

    // TODO: implement the following functions
    //logger.LogRequestingPiece(remoteID, idx);
    //sendPiece(remoteID, idx);
}

void Peer::handlePiece(int remoteID, const std::vector<unsigned char>& payload) {
    int32_t idx;
    memcpy(&idx, payload.data(), 4);
    idx = ntohl(idx);

    std::vector<unsigned char> data(payload.begin()+4, payload.end());

    // TODO: implement these functions
    //savePiece(idx, data) // write to file or memory
    //updateBitfield(idx)
    //broadcaseHave(idx);
    //logger.logDownloadingPiece(remoteID, idx, countPiecesOwned());
    //requestNextPiece(remoteID);
}

void Peer::handleHave(int remoteID, const std::vector<unsigned char> &payload) {
}

void Peer::handleBitfield(int remoteID, const std::vector<unsigned char> &payload) {
}
