#include <iostream>
#include <fstream>
#include <string>
#include "peer.h"
#include "Logger.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <peerID>" << std::endl;
        return 1;
    }

    int peerID = std::stoi(argv[1]);

    std::ifstream file("Common.cfg");
    if (!file.is_open()) {
        std::cerr << "Error: Could not open Common.cfg" << std::endl;
        return 1;
    }

    int NumberOfPreferredNeighbors = 0;
    int UnchokingInterval = 0;
    int OptimisticUnchokingInterval = 0;
    std::string FileName;
    long FileSize = 0;
    int PieceSize = 0;

    std::string key;
    while (file >> key) {
        if (key == "NumberOfPreferredNeighbors") {
            file >> NumberOfPreferredNeighbors;
        } else if (key == "UnchokingInterval") {
            file >> UnchokingInterval;
        } else if (key == "OptimisticUnchokingInterval") {
            file >> OptimisticUnchokingInterval;
        } else if (key == "FileName") {
            file >> FileName;
        } else if (key == "FileSize") {
            file >> FileSize;
        } else if (key == "PieceSize") {
            file >> PieceSize;
        } else {
            //basic error checking and skip line
            std::cerr << "Warning: Unknown key '" << key << "' in config file." << std::endl;
            std::string skip;
            std::getline(file, skip);
        }
    }
    file.close();


    std::cout << "NumberOfPreferredNeighbors: " << NumberOfPreferredNeighbors << "\n";
    std::cout << "UnchokingInterval: " << UnchokingInterval << "\n";
    std::cout << "OptimisticUnchokingInterval: " << OptimisticUnchokingInterval << "\n";
    std::cout << "FileName: " << FileName << "\n";
    std::cout << "FileSize: " << FileSize << "\n";
    std::cout << "PieceSize: " << PieceSize << "\n";
    std::cout << "Num of Pieces: " << FileSize/PieceSize << "\n";

    std::cout << "Common.cfg parsed successfully" << std::endl;

    //logging Examples

    // Create logger for this peer
    Logger logger(peerID);

    // Example 1: Log TCP connection made
    logger.logTCPConnectionMade(1002);

    // Example 2: Log TCP connection received
    logger.logTCPConnectionReceived(1003);

    // Example 3: Log change of preferred neighbors
    std::vector<int> preferredNeighbors = {1002, 1004, 1005};
    logger.logPreferredNeighborsChange(preferredNeighbors);

    // Example 4: Log optimistically unchoked neighbor
    logger.logOptimisticallyUnchokedNeighbor(1006);

    // Example 5: Log unchoking
    logger.logUnchoking(1002);

    // Example 6: Log choking
    logger.logChoking(1003);

    // Example 7: Log receiving 'have' message
    logger.logReceivingHave(1002, 5);

    // Example 8: Log receiving 'interested' message
    logger.logReceivingInterested(1004);

    // Example 9: Log receiving 'not interested' message
    logger.logReceivingNotInterested(1005);

    // Example 10: Log downloading a piece
    logger.logDownloadingPiece(1002, 10, 15);

    // Example 11: Log download complete
    logger.logDownloadComplete();


    Peer peer(peerID);
    peer.start();


    return 0;
}
