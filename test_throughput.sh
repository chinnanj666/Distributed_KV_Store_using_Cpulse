#!/bin/bash

# Test throughput for PUT and GET
echo "Testing PUT throughput"
for i in {1..1000}
do
    echo "PUT key$i value$i" | nc 127.0.0.1 8081
done

echo "Testing GET throughput"
for i in {1..1000}
do
    echo "GET key$i" | nc 127.0.0.1 8081
done
