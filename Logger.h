

#ifndef BIT_TORRENT_LOGGER_H
#define BIT_TORRENT_LOGGER_H


#include <string>
#include <fstream>
#include <mutex>
#include <vector>

class Peer;

class Logger {
private:
    std::string logFileName;
    std::ofstream logFile;
    std::mutex logMutex;
    int peerID;
    Peer& owner_peer_;

    // Helper method to get current timestamp in format: MM/DD/YYYY HH:MM:SS AM/PM
    std::string getCurrentTimestamp();

    // Thread-safe write to log file
    void writeLog(const std::string& message);

public:
    // Constructor: Opens log file for the given peer ID
    Logger(Peer& owner);


    // Destructor: Closes log file
    ~Logger();

    // 1. TCP connection made
    void logTCPConnectionMade(int peerID2);

    // 2. TCP connection received
    void logTCPConnectionReceived(int peerID2);

    // 3. Change of preferred neighbors
    void logPreferredNeighborsChange(const std::vector<int>& preferredNeighbors);

    // 4. Change of optimistically unchoked neighbor
    void logOptimisticallyUnchokedNeighbor(int neighborID);

    // 5. Unchoking
    void logUnchoking(int peerID2);

    // 6. Choking
    void logChoking(int peerID2);

    // 7. Receiving 'have' message
    void logReceivingHave(int peerID2, int pieceIndex);

    // 8. Receiving 'interested' message
    void logReceivingInterested(int peerID2);

    // 9. Receiving 'not interested' message
    void logReceivingNotInterested(int peerID2);

    // 10. Downloading a piece
    void logDownloadingPiece(int peerID2, int pieceIndex, int numPieces);

    // 11. Completion of download
    void logDownloadComplete();
};


#endif //BIT_TORRENT_LOGGER_H
