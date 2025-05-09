import socket
import time
import random
import os

def send_command(host, port, command):
    max_retries = 3
    for attempt in range(max_retries):
        try:
            print(f"Attempt {attempt + 1}: Connecting to {host}:{port}")
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                sock.settimeout(30)  # Increased to 30s
                sock.connect((host, port))
                sock.sendall((command + "\n").encode())
                print(f"Attempt {attempt + 1}: Sent to {host}:{port}: {command}")
                response = ""
                while True:
                    data = sock.recv(1024).decode()
                    if not data:
                        break
                    response += data
                    if "\n" in data:
                        break
                return response.strip()
        except Exception as e:
            print(f"Attempt {attempt + 1}: Error with {host}:{port}: {e}")
            if attempt < max_retries - 1:
                time.sleep(1)  # Wait 1s before retry
            continue
    return ""

def main():
    # Check if running in Docker
    is_docker = os.getenv("IN_DOCKER", "false").lower() == "true"
    
    # Node list: use container names in Docker, localhost for local execution
    if is_docker:
        nodes = [("kvstore1", 8081), ("kvstore2", 8082), ("kvstore3", 8083)]
    else:
        nodes = [("localhost", 8081), ("localhost", 8082), ("localhost", 8083)]
    
    print(f"Running {'in Docker' if is_docker else 'locally'}, nodes: {nodes}")
    
    # Wait for nodes to be ready
    time.sleep(40)  # 40s sleep

    # Test commands
    commands = [
        "PUT session:user1 {token:xyz123}",
        "GET session:user1",
        "PUT session:user2 {token:abc456}",
        "GET session:user2",
        "PUT session:user3 {token:def789}",
        "RANGE session:user1 session:user3",
        "PREFIX session:user",
        "REMOVE session:user2",
        "GET session:user2"
    ]

    for cmd in commands:
        # Randomly select a node
        node = random.choice(nodes)
        host, port = node
        response = send_command(host, port, cmd)
        print(f"Response: {response}")
        time.sleep(0.5)  # Delay for node communication

if __name__ == "__main__":
    main()
