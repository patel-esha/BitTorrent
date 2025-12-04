#pragma once
#include <string>
#include <vector>
#include <thread>
#include <map>
#include <set>
#include <unordered_map>
#include <mutex>
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
    std::string fileName;
    long fileSize;
    int pieceSize;
    int numPieces;
    std::vector<bool> bitfield;
    int optimisticallyUnchokedNeighbor = -1;
    std::unordered_map<int, int> peerSockets;
    std::unordered_map<int, NeighborState> neighborStates;
    std::map<int, std::vector<bool>> neighborBitfields;  // peerID -> their bitfield
    std::map<int, int> requestedPieces; // piece index -> peer ID we requested from
    std::mutex requestedPiecesMutex;
    std::mutex bitfieldMutex;
    std::mutex neighborMutex;
    std::mutex socketMutex;

    int loadPeerInfo(const std::string& peerFile);
    int loadCommonConfig(const std::string& configFile);
    void handleConnection(int sock, bool isInitiator);
    int listenForPeers();
    int connectToPeers();
    void sendHandshake(int socket);
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
    ssize_t readNBytes(int sock, void* buffer, size_t n);

    // bitfield helpers
    std::vector<unsigned char> bitfieldToBytes(); // convert bitfield to payload bytes
    std::vector<bool> bytesToBitfield(const std::vector<unsigned char>& payload, int expectedBits);

    void updateMyBitfield(int pieceIndex); // mark piece downloaded and broadcast HAVE
    bool peerHasInterestingPieces(int remoteID);
    void sendInterested(int remoteID);
    void sendNotInterested(int remoteID);

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

    // Choking/unchoking mechanism
    void selectPreferredNeighbors();
    void selectOptimisticallyUnchokedNeighbor();
    void preferredNeighborTimer();
    void optimisticUnchokeTimer();
    void updateDownloadRate(int remoteID, size_t bytes);
    bool allPeersComplete();
};