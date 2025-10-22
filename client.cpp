#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

constexpr int PORT = 8000;
constexpr int BUFFER_SIZE = 1024;

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Connection failed.\n";
        return 1;
    }

    std::cout << "Connected to server on port " << PORT << ".\n";

    char buffer[BUFFER_SIZE];
    std::string message;

    while (true) {
        std::cout << "Enter message (or 'exit'): ";
        std::getline(std::cin, message);
        if (message == "exit") break;

        send(sock, message.c_str(), message.size(), 0);

        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytesReceived = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytesReceived <= 0) {
            std::cout << "Server disconnected.\n";
            break;
        }

        std::cout << "Received: " << buffer << std::endl;
    }

    close(sock);
    return 0;
}
