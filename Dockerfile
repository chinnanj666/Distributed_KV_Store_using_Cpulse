# Use Ubuntu 20.04 as the base image for C++ compatibility
FROM ubuntu:20.04

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY kvstore.cpp main.cpp ./

# Compile the server
RUN g++ -o kvstore main.cpp -pthread -std=c++17

# Expose the port used by the server (dynamic via environment)
EXPOSE 8081

# Run the server with dynamic port
CMD ["sh", "-c", "./kvstore $PORT"]
