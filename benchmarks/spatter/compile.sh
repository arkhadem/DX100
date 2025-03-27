#!/bin/bash

# Check if an argument is provided
if [ $# -eq 0 ]; then
    echo "Error: No argument provided. Please specify 'FUNC' or 'GEM5'."
    exit 1
fi

# Set the appropriate build flag based on the argument
if [ "$1" == "FUNC" ]; then
    BUILD_FLAG="-DBUILD_FUNC=ON"
elif [ "$1" == "GEM5" ]; then
    BUILD_FLAG="-DBUILD_GEM5=ON"
else
    echo "Error: Invalid argument '$1'. Please specify 'FUNC' or 'GEM5'."
    exit 1
fi

# Run the cmake commands with the selected build flag
rm -rf build 2>&1 > /dev/null
mkdir -p build
cmake -DCMAKE_BUILD_TYPE=Release $BUILD_FLAG -B build -S .
cmake --build build -j
