#!/bin/bash

# Simple test script to launch SDR++ and verify it starts up correctly

# Set timeout (in seconds)
TIMEOUT=10

# Launch SDR++ in the background
./sdrpp &
PID=$!

# Wait for a short time to allow SDR++ to start up
sleep $TIMEOUT

# Check if SDR++ is still running
if kill -0 $PID 2>/dev/null; then
    echo "SDR++ started successfully and is running"
    # Kill SDR++ gracefully
    kill $PID
    # Wait for it to exit
    wait $PID 2>/dev/null
    echo "Test PASSED"
    exit 0
else
    echo "SDR++ failed to start or crashed"
    echo "Test FAILED"
    exit 1
fi
