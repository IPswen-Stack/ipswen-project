#!/bin/bash
set -e

cd "$(dirname "$0")"

# Optional: compile fakedns.so specifically for release without the ENABLE_DEBUG flag.
# If ENABLE_DEBUG is on, debug IO will ruin the benchmark times.
echo "Compiling benchmark release of fakedns.so..."
g++ -O3 -shared -fPIC -o fakedns_release.so fakedns.cpp -ldl

echo "Compiling benchmark program..."
g++ -O3 -o benchmark benchmark.cpp -ldl

echo "Running tests... (This might take a few seconds)"
# Run the benchmark strictly forcing standard loading
LD_PRELOAD=./fakedns_release.so ./benchmark
