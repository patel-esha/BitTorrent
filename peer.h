#pragma once
#include <string>
#include <vector>
#include <thread>

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

private:
    int peerId;
    std::vector<PeerInfo> peers;
    PeerInfo self;
    int loadPeerInfo(const std::string& fileName);
    int listenForPeers();
    int connectToPeers();
    void handleConnection(int socket);
};
