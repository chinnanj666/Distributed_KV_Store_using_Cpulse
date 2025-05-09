#include "kvstore.cpp"
#include <iostream>
#include <sstream>
#include <cstdlib>

bool isDebug() {
    return std::getenv("DEBUG") && std::string(std::getenv("DEBUG")) == "true";
}

std::vector<std::pair<std::string, int>> parseNodeList(const std::string& node_list) {
    std::vector<std::pair<std::string, int>> nodes;
    if (isDebug()) std::cerr << "Parsing node list: " << node_list << std::endl;
    std::istringstream iss(node_list);
    std::string node;
    while (std::getline(iss, node, ',')) {
        size_t colon = node.find(':');
        if (colon != std::string::npos) {
            std::string ip = node.substr(0, colon);
            int port = std::stoi(node.substr(colon + 1));
            if (isDebug()) std::cerr << "Added node: " << ip << ":" << port << std::endl;
            nodes.emplace_back(ip, port);
        } else {
            std::cerr << "Invalid node format: " << node << std::endl;
        }
    }
    return nodes;
}

int main(int argc, char* argv[]) {
    std::string ip = "0.0.0.0"; // Listen on all interfaces
    int port = 8081;
    std::vector<std::pair<std::string, int>> node_list;

    // Parse environment variable NODES
    if (const char* nodes_env = std::getenv("NODES")) {
        node_list = parseNodeList(nodes_env);
    } else {
        std::cerr << "NODES environment variable not set" << std::endl;
    }

    // Override port if provided as argument
    if (argc > 1) {
        port = std::stoi(argv[1]);
        if (isDebug()) std::cerr << "Using port from argument: " << port << std::endl;
    }

    if (isDebug()) std::cerr << "Initializing DistributedKVStore on " << ip << ":" << port << std::endl;
    DistributedKVStore kvstore(ip, port, node_list);
    if (!kvstore.startServer()) {
        std::cerr << "Failed to start server on " << ip << ":" << port << std::endl;
        return 1;
    }
    std::cerr << "Server running on " << ip << ":" << port << std::endl;
    kvstore.run();
    return 0;
}


// #include "kvstore.cpp"
// #include <iostream>

// int main() {
//     DistributedKVStore kvstore("127.0.0.1", 8081); // Changed port to 8081
//     if (!kvstore.startServer()) {
//         std::cerr << "Failed to start server" << std::endl;
//         return 1;
//     }
//     std::cout << "Server running on 127.0.0.1:8081" << std::endl;
//     kvstore.run();
//     return 0;
// }
