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

    std::ifstream file("../Common.cfg");
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
""
    //logging Examples

    int peer2 = 1008;
    Peer peer(peerID);
    peer.start();
    peer.logger.logTCPConnectionMade(peer2);
    peer.logger.logChoking(peer2);
    peer.logger.logUnchoking(peer2);


    return 0;
}
