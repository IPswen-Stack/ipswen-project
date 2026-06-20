#!/bin/bash

# Compile fakedns
echo "Compiling fakedns..."
g++ -fPIC -shared -o libfakedns.so fakedns.cpp -ldl -DENABLE_DEBUG

# Compile test programs
echo "Compiling test programs..."
g++ -o test_server test_server.cpp
g++ -o test_client test_client.cpp
g++ -o test_client_legacy test_client_legacy.cpp

# Run server in background
echo "Starting IPv6 server..."
./test_server &
SERVER_PID=$!

# Wait for server to start
sleep 1

# Run client with fakedns
echo "Starting IPv4 client with fakedns..."
LD_PRELOAD=./libfakedns.so ./test_client ip6-localhost

# Wait for server to exit (it handles one connection)
wait $SERVER_PID

# Restart server for legacy client
echo "Restarting IPv6 server..."
./test_server &
SERVER_PID=$!
sleep 1

echo "Starting Legacy IPv4 client with fakedns..."
LD_PRELOAD=./libfakedns.so ./test_client_legacy ip6-localhost

# Cleanup
kill $SERVER_PID 2>/dev/null
