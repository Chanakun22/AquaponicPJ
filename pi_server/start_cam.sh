#!/bin/bash

# Stop any existing camera process
sudo pkill -f cam_server.py 2>/dev/null
sudo pkill rpicam-vid 2>/dev/null
sudo pkill libcamera-vid 2>/dev/null
sleep 1

# Start Python MJPEG HTTP server (replaces rpicam-vid TCP which crashes on Trixie)
echo "Starting Camera Server..."
python3 "$(dirname "$0")/cam_server.py"
