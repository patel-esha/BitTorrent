#include "Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

#include "peer.h"


// Constructor
Logger::Logger(Peer& owner) : owner_peer_(owner) {
    int peerID_val = owner_peer_.getPeerId();
    this->peerID = peerID_val;
    logFileName = "log_peer_" + std::to_string(peerID) + ".log";
    logFile.open(logFileName, std::ios::out | std::ios::app);

    if (!logFile.is_open()) {
        std::cerr << "Error: Could not open log file: " << logFileName << std::endl;
    }
}



// Destructor
Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

// Get current timestamp in format: MM/DD/YYYY HH:MM:SS AM/PM
std::string Logger::getCurrentTimestamp() {
    time_t now = time(0);
    struct tm timeinfo;

#ifdef _WIN32
    localtime_s(&timeinfo, &now);
#else
    localtime_r(&now, &timeinfo);
#endif

    std::ostringstream oss;

    // Month/Day/Year
    oss << std::setfill('0') << std::setw(2) << (timeinfo.tm_mon + 1) << "/"
        << std::setfill('0') << std::setw(2) << timeinfo.tm_mday << "/"
        << (timeinfo.tm_year + 1900) << " ";

    // Hour (12-hour format)
    int hour12 = timeinfo.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;

    oss << std::setfill('0') << std::setw(2) << hour12 << ":"
        << std::setfill('0') << std::setw(2) << timeinfo.tm_min << ":"
        << std::setfill('0') << std::setw(2) << timeinfo.tm_sec << " ";

    // AM/PM
    oss << (timeinfo.tm_hour >= 12 ? "PM" : "AM");

    return oss.str();
}

// Thread-safe write to log file
void Logger::writeLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        logFile << message << std::endl;
        logFile.flush(); // Ensure immediate write
    }
}

// 1. TCP connection made
void Logger::logTCPConnectionMade(int peerID2) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " makes a connection to Peer " << peerID2 << ".";
    writeLog(oss.str());
}

// 2. TCP connection received
void Logger::logTCPConnectionReceived(int peerID2) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " is connected from Peer " << peerID2 << ".";
    writeLog(oss.str());
}

// 3. Change of preferred neighbors
void Logger::logPreferredNeighborsChange(const std::vector<int>& preferredNeighbors) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " has the preferred neighbors ";

    for (size_t i = 0; i < preferredNeighbors.size(); ++i) {
        oss << preferredNeighbors[i];
        if (i < preferredNeighbors.size() - 1) {
            oss << ",";
        }
    }
    oss << ".";

    writeLog(oss.str());
}

// 4. Change of optimistically unchoked neighbor
void Logger::logOptimisticallyUnchokedNeighbor(int neighborID) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " has the optimistically unchoked neighbor " << neighborID << ".";
    writeLog(oss.str());
}

// 5. Unchoking
void Logger::logUnchoking(int peerID2) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " is unchoked by " << peerID2 << ".";
    writeLog(oss.str());
}

// 6. Choking
void Logger::logChoking(int peerID2) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " is choked by " << peerID2 << ".";
    writeLog(oss.str());
}

// 7. Receiving 'have' message
void Logger::logReceivingHave(int peerID2, int pieceIndex) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " received the 'have' message from " << peerID2
        << " for the piece " << pieceIndex << ".";
    writeLog(oss.str());
}

// 8. Receiving 'interested' message
void Logger::logReceivingInterested(int peerID2) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " received the 'interested' message from " << peerID2 << ".";
    writeLog(oss.str());
}

// 9. Receiving 'not interested' message
void Logger::logReceivingNotInterested(int peerID2) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " received the 'not interested' message from " << peerID2 << ".";
    writeLog(oss.str());
}

// 10. Downloading a piece
void Logger::logDownloadingPiece(int peerID2, int pieceIndex, int numPieces) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " has downloaded the piece " << pieceIndex << " from " << peerID2
        << ". Now the number of pieces it has is " << numPieces << ".";
    writeLog(oss.str());
}

// 11. Completion of download
void Logger::logDownloadComplete() {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "]: Peer " << peerID
        << " has downloaded the complete file.";
    writeLog(oss.str());
}