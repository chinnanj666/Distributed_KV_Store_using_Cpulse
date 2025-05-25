#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <poll.h>

bool connectToServer(int& sock) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
   
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8081);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (connect(sock, (sockaddr*)&server, sizeof(server)) == -1) {
        std::cerr << "Failed to connect to server: " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }
    std::cerr << "Connected to server 127.0.0.1:8081" << std::endl;
    return true;
}

std::string sendRequest(int sock, const std::string& request) {
    std::string msg = request + "\n";
    if (write(sock, msg.c_str(), msg.length()) == -1) {
        std::cerr << "Failed to send request: " << strerror(errno) << std::endl;
        return "";
    }
    std::cerr << "Sent request: " << request << std::endl;

    std::vector<char> buffer(1024);
    std::string response;
    struct pollfd fd;
    fd.fd = sock;
    fd.events = POLLIN;

    // Retry up to 3 times with timeout
    for (int attempt = 0; attempt < 3; ++attempt) {
        int ret = poll(&fd, 1, 1000); // 1-second timeout
        if (ret == -1) {
            std::cerr << "Poll error: " << strerror(errno) << std::endl;
            return "";
        }
        if (ret == 0) {
            std::cerr << "Timeout waiting for response, attempt " << attempt + 1 << std::endl;
            continue;
        }

        ssize_t bytes = read(sock, buffer.data(), buffer.size() - 1);
        if (bytes == -1) {
            std::cerr << "Failed to read response: " << strerror(errno) << std::endl;
            return "";
        }
        if (bytes == 0) {
            std::cerr << "Server disconnected" << std::endl;
            return "";
        }

        buffer[bytes] = '\0';
        response += buffer.data();
        if (response.back() == '\n') break; // Stop at newline
    }

    if (response.empty()) {
        std::cerr << "No response after retries" << std::endl;
        return "";
    }

    // Remove trailing newline
    if (!response.empty() && response.back() == '\n') {
        response.pop_back();
    }
    std::cerr << "Received response: " << response << std::endl;
    return response;
}

int main() {
    int sock = -1;
    if (!connectToServer(sock)) {
        return 1;
    }

    std::string command;
    while (true) {
        std::cout << "Enter command (PUT <key> <value>, GET <key>, RANGE <start> <end>, PREFIX <prefix>, or QUIT): ";
        std::getline(std::cin, command);
        if (command == "QUIT") break;
        if (command.empty()) continue;

        std::string response = sendRequest(sock, command);
        if (response.empty()) {
            std::cerr << "Connection lost, attempting to reconnect" << std::endl;
            close(sock);
            if (!connectToServer(sock)) {
                std::cerr << "Reconnection failed, exiting" << std::endl;
                break;
            }
            // Retry the command
            response = sendRequest(sock, command);
        }

        if (!response.empty()) {
            std::cout << "Response: " << response << std::endl;
        } else {
            std::cout << "No response received" << std::endl;
        }
    }

    if (sock != -1) {
        close(sock);
        std::cerr << "Closed client socket" << std::endl;
    }
    return 0;
}
