#pragma once
#include <string>
#include <vector>
#include <thread>
#include <map>
#include <set>
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

struct Message {
    uint32_t length;        // includes type byte + payload
    unsigned char type;     // message ID
    std::vector<unsigned char> payload;
};

struct NeighborState {
    bool peerChoking = true;      // Is the remote peer choking us
    bool peerInterested = false;  // Is the remote peer interested in us
    bool amChoking = true;        // Are we choking them
    bool amInterested = false;    // Are we interested in them
    double downloadRate = 0.0; // bytes/sec provided
    long bytesDownloaded = 0; // For best Neighbor
    long bytesUploaded = 0; //
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
    //std::unordered_map<int, std::vector<bool>> neighborBitfields;
    std::unordered_map<int, int> peerSockets;
    std::unordered_map<int, NeighborState> neighborStates;
    std::map<int, std::vector<bool>> neighborBitfields;  // peerID -> their bitfield
    std::set<int> requestedPieces; // pieces we've already requested
    std::mutex requestedPiecesMutex;

    int loadPeerInfo(const std::string& fileName);
    int loadCommonConfig(const std::string& fileName);
    int listenForPeers();
    int connectToPeers();
    void handleConnection(int socket);
    std::vector<unsigned char> createHandshake();
    void sendBitfield(int socket);
    bool receiveHandshake(int socket, int &remotePeerID);
    bool receiveMessage(int socket, Message &msg);
    bool sendMessage(int socket, unsigned char type, const std::vector<unsigned char>& payload);
    void handleMessage(int remoteID, const Message &msg);
    void handleInterested(int remoteID);
    void handleNotInterested(int remoteID);
    void handleChoke(int remoteID);
    void handleUnchoke(int remoteID);
    void handleRequest(int remoteID, const std::vector<unsigned char>& payload);
    void handlePiece(int remoteID, const std::vector<unsigned char>& payload);
    void handleHave(int remoteID, const std::vector<unsigned char>& payload);
    void handleBitfield(int remoteID, const std::vector<unsigned char>& payload);
    // File handling
    void savePiece(int pieceIndex, const std::vector<unsigned char>& data);
    std::vector<unsigned char> loadPiece(int pieceIndex);
    std::string getPieceFilePath(int pieceIndex);

    // Piece exchange
    void requestNextPiece(int remoteID);
    void sendPiece(int remoteID, int pieceIndex);
    void broadcastHave(int pieceIndex);
    int selectRandomPiece(int remoteID);  // Returns -1 if no piece available
    bool hasCompletedDownload();
    int countPiecesOwned();

};
