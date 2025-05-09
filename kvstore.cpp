#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <queue>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <fcntl.h>

// Simplified MurmurHash3 for consistent hashing
uint32_t MurmurHash3_x86_32(const void* key, int len, uint32_t seed) {
    const uint8_t* data = (const uint8_t*)key;
    const int nblocks = len / 4;
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);
    for (int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> 19);
        h1 = h1 * 5 + 0xe6546b64;
    }

    const uint8_t* tail = (const uint8_t*)(data + nblocks * 4);
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = (k1 << 15) | (k1 >> 17);
                k1 *= c2;
                h1 ^= k1;
    }

    h1 ^= len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    return h1;
}

// Lock-free queue for write buffering
template<typename T>
class LockFreeQueue {
private:
    std::vector<T> buffer;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    size_t capacity;

public:
    LockFreeQueue(size_t size) : capacity(size), head(0), tail(0) {
        buffer.resize(size);
    }

    bool enqueue(const T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % capacity;
        if (next_tail == head.load(std::memory_order_acquire)) return false; // Queue full
        buffer[current_tail] = item;
        tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool dequeue(T& item) {
        size_t current_head = head.load(std::memory_order_relaxed);
        if (current_head == tail.load(std::memory_order_acquire)) return false; // Queue empty
        item = buffer[current_head];
        head.store((current_head + 1) % capacity, std::memory_order_release);
        return true;
    }
};

// Simplified r-index for range queries and prefix scans
class RIndex {
private:
    struct Run {
        std::string prefix;
        uint32_t start;
        uint32_t length;
    };
    std::vector<Run> runs;
    std::vector<std::string> keys; // Sorted keys for binary search

public:
    void build(const std::unordered_map<std::string, std::string>& store) {
        keys.clear();
        runs.clear();
        for (const auto& [key, _] : store) {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());

        // Build runs for consecutive keys with common prefixes
        if (!keys.empty()) {
            std::string last_prefix = keys[0];
            uint32_t start = 0;
            for (size_t i = 1; i <= keys.size(); ++i) {
                if (i == keys.size() || !hasCommonPrefix(last_prefix, keys[i])) {
                    runs.push_back({last_prefix, start, static_cast<uint32_t>(i - start)});
                    if (i < keys.size()) {
                        last_prefix = keys[i];
                        start = i;
                    }
                }
            }
        }
    }

    bool hasCommonPrefix(const std::string& a, const std::string& b) {
        size_t min_len = std::min(a.length(), b.length());
        for (size_t i = 0; i < min_len; ++i) {
            if (a[i] != b[i]) return false;
        }
        return true;
    }

    std::vector<std::string> rangeQuery(const std::string& start, const std::string& end) {
        std::vector<std::string> result;
        auto lower = std::lower_bound(keys.begin(), keys.end(), start);
        auto upper = std::upper_bound(keys.begin(), keys.end(), end);
        for (auto it = lower; it != upper; ++it) {
            result.push_back(*it);
        }
        return result;
    }

    std::vector<std::string> prefixScan(const std::string& prefix) {
        std::vector<std::string> result;
        for (const auto& run : runs) {
            if (run.prefix.find(prefix) == 0) {
                for (uint32_t i = run.start; i < run.start + run.length; ++i) {
                    result.push_back(keys[i]);
                }
            }
        }
        return result;
    }
};

class Node {
public:
    std::string ip;
    int port;
    uint32_t hash;

    Node(const std::string& ip, int port) : ip(ip), port(port) {
        std::string id = ip + ":" + std::to_string(port);
        hash = MurmurHash3_x86_32(id.c_str(), id.length(), 0);
    }
};

class DistributedKVStore {
private:
    std::unordered_map<std::string, std::string> store;
    RIndex rindex;
    LockFreeQueue<std::pair<std::string, std::string>> write_buffer;
    std::vector<Node> nodes;
    int server_fd;
    std::string ip;
    int port;
    std::atomic<bool> running;

    uint32_t hashKey(const std::string& key) {
        return MurmurHash3_x86_32(key.c_str(), key.length(), 0);
    }

    Node* findNodeForKey(const std::string& key) {
        if (nodes.empty()) return nullptr;
        uint32_t keyHash = hashKey(key);
        Node* target = &nodes[0];
        for (auto& node : nodes) {
            if (node.hash >= keyHash && node.hash < target->hash) {
                target = &node;
            }
        }
        return target;
    }

    std::string sendToNode(const std::string& ip, int port, const std::string& request) {
        std::cerr << "Attempting to send to " << ip << ":" << port << ": " << request << std::endl;
        const int max_retries = 3;
        for (int attempt = 1; attempt <= max_retries; ++attempt) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
                std::cerr << "Attempt " << attempt << ": Failed to create socket: " << strerror(errno) << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
                continue;
            }

            sockaddr_in server;
            server.sin_family = AF_INET;
            server.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &server.sin_addr);

            // Set connect timeout
            struct timeval tv = {10, 0}; // 10s timeout
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            // Check socket buffer status
            int pending;
            ioctl(sock, TIOCOUTQ, &pending);
            std::cerr << "Attempt " << attempt << ": Socket send buffer pending: " << pending << " bytes" << std::endl;

            if (connect(sock, (sockaddr*)&server, sizeof(server)) == -1) {
                std::cerr << "Attempt " << attempt << ": Failed to connect to " << ip << ":" << port << ": " << strerror(errno) << std::endl;
                close(sock);
                std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
                continue;
            }

            std::string msg = request + "\n";
            if (write(sock, msg.c_str(), msg.length()) == -1) {
                std::cerr << "Attempt " << attempt << ": Failed to send to " << ip << ":" << port << ": " << strerror(errno) << std::endl;
                close(sock);
                std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
                continue;
            }

            char buffer[1024] = {0};
            int bytes = read(sock, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) {
                std::cerr << "Attempt " << attempt << ": Failed to read from " << ip << ":" << port << ": " << (bytes == 0 ? "Connection closed" : strerror(errno)) << std::endl;
                close(sock);
                std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
                continue;
            }

            close(sock);
            std::cerr << "Received from " << ip << ":" << port << ": " << std::string(buffer, bytes) << std::endl;
            return std::string(buffer, bytes);
        }

        std::cerr << "All " << max_retries << " attempts to " << ip << ":" << port << " failed" << std::endl;
        return "ERROR";
    }

    void processWriteBuffer() {
        while (running) {
            std::pair<std::string, std::string> item;
            if (write_buffer.dequeue(item)) {
                store[item.first] = item.second;
                rindex.build(store);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void logFileDescriptors() {
        while (running) {
            std::string cmd = "ls /proc/" + std::to_string(getpid()) + "/fd | wc -l";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buffer[128];
                if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    std::cerr << "Current file descriptors: " << std::stoi(buffer) << std::endl;
                }
                pclose(pipe);
            }
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }

public:
    DistributedKVStore(const std::string& ip, int port, const std::vector<std::pair<std::string, int>>& node_list)
        : ip(ip), port(port), write_buffer(1000), running(true) {
        // Increase file descriptor limit
        struct rlimit limit;
        getrlimit(RLIMIT_NOFILE, &limit);
        limit.rlim_cur = limit.rlim_max = 4096;
        if (setrlimit(RLIMIT_NOFILE, &limit) == -1) {
            std::cerr << "Failed to set file descriptor limit: " << strerror(errno) << std::endl;
        } else {
            std::cerr << "Set file descriptor limit to 4096" << std::endl;
        }

        nodes.emplace_back(ip, port);
        for (const auto& [node_ip, node_port] : node_list) {
            addNode(node_ip, node_port);
        }
        std::thread(&DistributedKVStore::processWriteBuffer, this).detach();
        std::thread(&DistributedKVStore::logFileDescriptors, this).detach();
    }

    bool startServer() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            std::cerr << "Failed to set socket options: " << strerror(errno) << std::endl;
            close(server_fd);
            return false;
        }

        // Set non-blocking
        int flags = fcntl(server_fd, F_GETFL, 0);
        if (flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            std::cerr << "Failed to set non-blocking: " << strerror(errno) << std::endl;
            close(server_fd);
            return false;
        }

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
            std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
            close(server_fd);
            return false;
        }

        if (listen(server_fd, 100) == -1) { // Increased backlog
            std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
            close(server_fd);
            return false;
        }

        return true;
    }

    void addNode(const std::string& ip, int port) {
        nodes.emplace_back(ip, port);
        std::sort(nodes.begin(), nodes.end(), 
            [](const Node& a, const Node& b) { return a.hash < b.hash; });
    }

    bool put(const std::string& key, const std::string& value) {
        Node* target = findNodeForKey(key);
        if (!target) return false;
        if (target->ip == ip && target->port == port) {
            store[key] = value;
            rindex.build(store);
            return true;
        } else {
            std::string request = "PUT " + key + " " + value;
            std::string response = sendToNode(target->ip, target->port, request);
            return response == "OK";
        }
    }

    std::string get(const std::string& key) {
        Node* target = findNodeForKey(key);
        if (!target) return "ERROR";
        if (target->ip == ip && target->port == port) {
            auto it = store.find(key);
            return it != store.end() ? it->second : "";
        } else {
            std::string request = "GET " + key;
            return sendToNode(target->ip, target->port, request);
        }
    }

    bool remove(const std::string& key) {
        Node* target = findNodeForKey(key);
        if (!target) return false;
        if (target->ip == ip && target->port == port) {
            return store.erase(key) > 0;
        } else {
            std::string request = "REMOVE " + key;
            std::string response = sendToNode(target->ip, target->port, request);
            return response == "OK";
        }
    }

    std::vector<std::string> rangeQuery(const std::string& start, const std::string& end) {
        std::vector<std::string> result;
        for (auto& node : nodes) {
            if (node.ip == ip && node.port == port) {
                auto local_result = rindex.rangeQuery(start, end);
                result.insert(result.end(), local_result.begin(), local_result.end());
            } else {
                std::string request = "RANGE " + start + " " + end;
                std::string response = sendToNode(node.ip, node.port, request);
                if (response != "ERROR" && response != "NONE") {
                    std::istringstream iss(response);
                    std::string key;
                    while (iss >> key) {
                        result.push_back(key);
                    }
                }
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::vector<std::string> prefixScan(const std::string& prefix) {
        std::vector<std::string> result;
        for (auto& node : nodes) {
            if (node.ip == ip && node.port == port) {
                auto local_result = rindex.prefixScan(prefix);
                result.insert(result.end(), local_result.begin(), local_result.end());
            } else {
                std::string request = "PREFIX " + prefix;
                std::string response = sendToNode(node.ip, node.port, request);
                if (response != "ERROR" && response != "NONE") {
                    std::istringstream iss(response);
                    std::string key;
                    while (iss >> key) {
                        result.push_back(key);
                    }
                }
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    void run() {
        while (running) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                std::cerr << "Failed to accept client: " << strerror(errno) << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            std::cerr << "Accepted client: " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

            // Set client timeout
            struct timeval tv = {10, 0}; // 10s timeout
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            char buffer[1024] = {0};
            int bytes = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes == -1) {
                std::cerr << "Failed to read from client: " << strerror(errno) << std::endl;
                close(client_fd);
                continue;
            }
            if (bytes == 0) {
                std::cerr << "Client disconnected" << std::endl;
                close(client_fd);
                continue;
            }

            std::string request(buffer, bytes);
            request.erase(std::remove(request.begin(), request.end(), '\n'), request.end());
            std::cerr << "Received request: \"" << request << "\"" << std::endl;

            std::istringstream iss(request);
            std::string command, key, value, start, end, prefix;
            iss >> command;

            std::string response;
            try {
                if (command == "PUT") {
                    iss >> key >> value;
                    if (key.empty() || value.empty()) {
                        response = "ERROR: PUT requires key and value";
                        std::cerr << "Invalid PUT request: key or value missing" << std::endl;
                    } else {
                        response = put(key, value) ? "OK" : "ERROR";
                    }
                } else if (command == "GET") {
                    iss >> key;
                    if (key.empty()) {
                        response = "ERROR: GET requires key";
                        std::cerr << "Invalid GET request: key missing" << std::endl;
                    } else {
                        response = get(key);
                        if (response.empty()) response = "NOT_FOUND";
                    }
                } else if (command == "REMOVE") {
                    iss >> key;
                    if (key.empty()) {
                        response = "ERROR: REMOVE requires key";
                        std::cerr << "Invalid REMOVE request: key missing" << std::endl;
                    } else {
                        response = remove(key) ? "OK" : "NOT_FOUND";
                    }
                } else if (command == "RANGE") {
                    iss >> start >> end;
                    if (start.empty() || end.empty()) {
                        response = "ERROR: RANGE requires start and end keys";
                        std::cerr << "Invalid RANGE request: start or end missing" << std::endl;
                    } else {
                        auto keys = rangeQuery(start, end);
                        for (const auto& k : keys) {
                            response += k + " ";
                        }
                        if (response.empty()) response = "NONE";
                    }
                } else if (command == "PREFIX") {
                    iss >> prefix;
                    if (prefix.empty()) {
                        response = "ERROR: PREFIX requires prefix";
                        std::cerr << "Invalid PREFIX request: prefix missing" << std::endl;
                    } else {
                        auto keys = prefixScan(prefix);
                        for (const auto& k : keys) {
                            response += k + " ";
                        }
                        if (response.empty()) response = "NONE";
                    }
                } else {
                    response = "INVALID_COMMAND";
                    std::cerr << "Invalid command: " << command << std::endl;
                }
            } catch (const std::exception& e) {
                response = "ERROR: Server exception";
                std::cerr << "Exception processing request: " << e.what() << std::endl;
            }

            std::cerr << "Sending response: \"" << response << "\"" << std::endl;
            std::string msg = response + "\n";
            ssize_t sent = write(client_fd, msg.c_str(), msg.length());
            if (sent == -1) {
                std::cerr << "Failed to write to client: " << strerror(errno) << std::endl;
            } else if (sent != static_cast<ssize_t>(msg.length())) {
                std::cerr << "Incomplete write to client: sent " << sent << " of " << msg.length() << " bytes" << std::endl;
            }

            shutdown(client_fd, SHUT_WR);
            close(client_fd);
            std::cerr << "Closed client connection" << std::endl;
        }
    }

    ~DistributedKVStore() {
        running = false;
        close(server_fd);
    }
};
