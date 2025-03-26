#!/bin/bash

# Directory containing the JSON files
APP_TRACES_DIR="./standard-suite/lanl-traces"

# Check if spatter executable exists
if [ ! -f "./build_serial/spatter" ]; then
    echo "Error: ./build_serial/spatter executable not found!"
    exit 1
fi

# Check if the directory exists
if [ ! -d "$APP_TRACES_DIR" ]; then
    echo "Error: Directory $APP_TRACES_DIR does not exist!"
    exit 1
fi

# Loop through all JSON files in the app-traces directory
for json_file in "$APP_TRACES_DIR"/*.json; do
    # Check if there are no JSON files
    if [ ! -e "$json_file" ]; then
        echo "No JSON files found in $APP_TRACES_DIR"
        exit 1
    fi

     # Skip files ending with _gpu.json
    if [[ "$json_file" == *_gpu.json ]]; then
        # echo "Skipping $json_file"
        continue
    fi

    # Run the command with each JSON file
    echo "Running ./build_serial/spatter with $json_file"
    ./build_serial/spatter -f "$json_file"

    # Capture the exit status of the last command
    if [ $? -ne 0 ]; then
        echo "Error running ./build_serial/spatter with $json_file"
        exit 1
    fi
done

echo "All JSON files processed successfully."