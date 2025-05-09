#!/bin/bash

# Check running containers
echo "Running containers:"
docker ps

# Check container logs
echo -e "\nLogs for kvstore1:"
docker logs kvstore1

echo -e "\nLogs for kvstore2:"
docker logs kvstore2

echo -e "\nLogs for kvstore3:"
docker logs kvstore3

# Test connectivity to each node
echo -e "\nTesting connectivity to kvstore1:8081"
nc -z -v localhost 8081

echo -e "\nTesting connectivity to kvstore2:8082"
nc -z -v localhost 8082

echo -e "\nTesting connectivity to kvstore3:8083"
nc -z -v localhost 8083

# Check Docker network
echo -e "\nDocker network details:"
docker network inspect kvstore-network
