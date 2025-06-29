# Distributed Key-Value Store

A multi-node distributed key-value store implemented in C++ with support for `PUT`, `GET`, `REMOVE`, `RANGE`, and `PREFIX` commands. Uses consistent hashing for key distribution and an r-index for range and prefix queries.

## Prerequisites
- **Docker**: Install Docker Desktop on macOS (https://docs.docker.com/desktop/install/mac-install/).
- **Python**: Python 3.9+ for `test_client.py`.
- **Files**:
  - `kvstore.cpp`
  - `main.cpp`
  - `Dockerfile`
  - `docker-compose.yml`
  - `test_client.py`
  - `debug_nodes.sh`

## Directory Structure
```
Distributed_KV_Store/
├── kvstore.cpp
├── main.cpp
├── Dockerfile
├── docker-compose.yml
├── test_client.py
├── debug_nodes.sh
├── README.md
```

## Deployment Instructions

### Run with Docker Compose (Recommended)
1. Navigate to the project directory:
   ```bash
   cd Distributed_KV_Store
   ```
2. Start the services:
   ```bash
   docker-compose up --build
   ```
3. Expected output:
   - Server logs: `Server running on 0.0.0.0:8081` for each node.
   - Client logs:
     ```
     Running in Docker, nodes: [('kvstore1', 8081), ('kvstore2', 8082), ('kvstore3', 8083)]
     Attempt 1: Sent to kvstore1:8081: PUT session:user1 {token:xyz123}
     Response: OK
     ...
     ```
4. Stop:
   ```bash
   docker-compose down
   ```

### Run Nodes and Client Locally
1. Start nodes:  
   ```bash
    docker-compose up -d kvstore1 kvstore2 kvstore3
   ```
2. Run the client:
   ```bash
    python3 test_client.py
   ```
3. Expected output:
   ```
   Running locally, nodes: [('localhost', 8081), ('localhost', 8082), ('localhost', 8083)]
   Attempt 1: Sent to localhost:8081: PUT session:user1 {token:xyz123}
   Response: OK
   ...
   ```
4. Stop:
   ```bash
   docker-compose down
   ```

### Manual Testing
- Send commands to any node:
  ```bash
  echo "PUT session:user4 {token:ghi012}" | nc localhost 8081
  echo "GET session:user4" | nc localhost 8082
  ```
- Expected:
  ```
  OK
  {token:ghi012}
  ```

## Troubleshooting
1. **Unhealthy Nodes**:
   - Error: `container kvstoreX is unhealthy`
   - Check logs:
     ```bash
     docker logs kvstoreX
     ```
   - Test health check manually:
     ```bash
     docker exec kvstoreX netstat -tuln | grep :808X
     ```
   - Increase `start_period` to 70s in `docker-compose.yml`.
   - Run debug script:
     ```bash
     chmod +x debug_nodes.sh
     ./debug_nodes.sh
     ```

2. **kvstore1/kvstore3 Timeouts**:
   - Error: `Error with localhost:8081: timed out` or `Error with localhost:8083: timed out`
   - Check logs:
     ```bash
     docker logs kvstore1
     docker logs kvstore3
     ```
   - Inspect containers:
     ```bash
     docker inspect kvstore1
     docker inspect kvstore3
     ```
   - Check file descriptors:
     ```bash
     docker exec kvstore1 lsof -p 1 | wc -l
     docker exec kvstore3 lsof -p 1 | wc -l
     ```
   - Check listening sockets:
     ```bash 
     docker exec kvstore1 netstat -tuln | grep :8081
     docker exec kvstore3 netstat -tuln | grep :8083
     ```
   - Restart containers:
     ```bash
     docker restart kvstore1
     docker restart kvstore3
     ```
   - Test directly:
     ```bash
     echo "PUT test:key value" | nc localhost 8081
     echo "PUT test:key value" | nc localhost 8083
     ```

3. **Client Script Errors**:
   - Error: `name 'SOCK_STREAM' is not defined`
   - Verify `test_client.py`:
     ```bash
     cat test_client.py
     ```
     - Ensure `socket.SOCK_STREAM` is used in `socket.socket(socket.AF_INET, socket.SOCK_STREAM)`.
   - Test Python environment:
     ```bash
     python3 -c "import socket; print(socket.SOCK_STREAM)"
     ```
     - Expected: `1`

4. **Inter-Node Communication Failures**:
   - Error: `Failed to connect to kvstoreX:808X` or `Resource temporarily unavailable`
   - Verify network:
     ```bash
     docker network ls
     docker network inspect kvstore-network
     ```
   - Test DNS resolution:
     ```bash
     docker run --rm --network kvstore-network python:3.9-slim nslookup kvstore1
     ```
   - Test connectivity:
     ```bash
     docker run --rm --network kvstore-network python:3.9-slim nc -z kvstore1 8081
     ```
   - Recreate network:
     ```bash
     docker network rm kvstore-network
     docker-compose up -d
     ```

5. **Port Conflicts**:
   - Check:
     ```bash
     lsof -i :8081
     lsof -i :8083
     ```
   - Kill conflicting processes or change ports in `docker-compose.yml`.

## Test Plan
1. **Key Distribution**: Verify keys are stored on different nodes.
   - Send: `PUT session:user1 {token:xyz123}` to `localhost:8081`.
   - Check logs to confirm storage node.
2. **Request Forwarding**: Send `GET` to a non-owning node and check response.
   - Send: `GET session:user1` to `localhost:8082`.
   - Expected: `{token:xyz123}`.
3. **Range Queries**: Run `RANGE session:user1 session:user3`.
   - Expected: `session:user1 session:user2 session:user3`.
4. **Prefix Scans**: Run `PREFIX session:user`.
   - Expected: `session:user1 session:user2 session:user3`.
5. **Deletion**: Run `REMOVE session:user2` and verify `NOT_FOUND`.
