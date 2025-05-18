#!/bin/bash
# Entry script for running the server and client

# Function to clean up processes and fifos on exit
cleanup() {
  echo "Cleaning up processes and files..."
  pkill -f "./server"
  rm -f FIFO_* doc.md
  exit 0
}

# Trap signals for clean shutdown
trap cleanup SIGINT SIGTERM

# Build the application
cd /app
make clean
make all

# Start the server in background with the provided time interval
./server ${1:-1000} &
SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"

# Keep container running
echo "Container is running. You can now use docker exec to run clients."
echo "Example: docker exec -it collab_server ./client $SERVER_PID <username>"
echo "Press Ctrl+C to stop the server and exit"

# Wait for the server to exit or for a signal
wait $SERVER_PID

# Clean up before exiting
cleanup
