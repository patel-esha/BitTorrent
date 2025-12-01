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

    int loadPeerInfo(const std::string& fileName);
    int loadCommonConfig(const std::string& fileName);
    int listenForPeers();
    int connectToPeers();
    void handleConnection(int socket);

};
