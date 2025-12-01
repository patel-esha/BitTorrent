#pragma once
#include <string>
#include <vector>
#include <thread>
#include <unordered_map>
#include "Logger.h"

struct PeerInfo {
    int id;
    std::string hostName;
    int port;
    bool hasFile;
};

struct Handshake {
    char header[18];     // "P2PFILESHARINGPROJ"
    char zeros[10];      // all zero
    int32_t peerID;      // network byte order
};

class Peer {
public:
    explicit Peer(int peerId);
    void start();
    int getPeerId();
    Logger logger;

private:
    int peerId;
    std::vector<PeerInfo> peers;
    PeerInfo self;
    int numPreferredNeighbors;
    int unchokingInterval;
    int optimisticUnchokingInterval;
    std::string name;
    long size;
    int pieceSize;
    std::vector<bool> bitfield;
    std::unordered_map<int, std::vector<bool>> neighborBitfields;
    std::unordered_map<int, int> peerSockets;

    int loadPeerInfo(const std::string& fileName);
    int loadCommonConfig(const std::string& fileName);
    int listenForPeers();
    int connectToPeers();
    void handleConnection(int socket);
    std::vector<unsigned char> createHandshake();
    void sendBitfield(int socket);
    bool receiveHandshake(int socket, int &remotePeerID);
};
