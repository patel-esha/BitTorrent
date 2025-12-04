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

    //logging Examples

    int peer2 = 1008;
    Peer peer(peerID);
    peer.start();

    return 0;
}
