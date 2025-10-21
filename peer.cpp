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

Peer::Peer(int id) : peerId(id) {
    loadPeerInfo("PeerInfo.cfg");
}

void signalHandler(int) {
    running = false;
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

int Peer::listenForPeers() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(self.port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Bind failed.\n";
        return 1;
    }

    if (listen(serverSocket, 5) < 0) {
        std::cerr << "Listen failed.\n";
        return 1;
    }

    std::cout << "Server listening on port " << self.port << "...\n";

    std::vector<std::thread> threads;
    int clientNum = 1;

    signal(SIGINT, signalHandler);

    while (running) {
        sockaddr_in clientAddr{};
        socklen_t clientSize = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientSize);

        if (clientSocket >= 0) {
            std::thread(&Peer::handleConnection, this, clientNum++);
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
            serverAddr.sin_port = htons(self.port);
            inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

            if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
                std::cerr << "Connection failed.\n";
                return 1;
            }

            std::thread(&Peer::handleConnection, this, sock).detach();
        }
    }
    return 0;
}

void Peer::handleConnection(int socket) {
    char buffer[BUFFER_SIZE];

    //std::cout << "Client " << clientNum << " connected!" << std::endl;

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytesRead = recv(socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesRead <= 0) {
            //std::cout << "Client " << clientNum << " disconnected.\n";
            break;
        }

        //std::cout << "Received from client " << clientNum << ": " << buffer << std::endl;

        // convert to uppercase
        for (int i = 0; i < bytesRead; ++i)
            buffer[i] = std::toupper(buffer[i]);

        // send back
        send(socket, buffer, bytesRead, 0);
    }

    close(socket);
}