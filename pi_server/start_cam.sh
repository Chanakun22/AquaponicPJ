#!/bin/bash

# Stop any existing camera process
pkill libcamera-vid

# Start libcamera-vid in background
# --inline: Embeds headers for stream
# --listen: Waits for connection
# -t 0: No timeout (run forever)
# --width 1280 --height 720: 720p Resolution
# --framerate 15: Save CPU/Bandwidth
# --codec mjpeg: MJPEG format for browser compatibility

echo "Starting 720p Live Stream..."
libcamera-vid -t 0 --inline --listen -o tcp://0.0.0.0:8081 --width 1280 --height 720 --framerate 15 --codec mjpeg &

echo "Stream started on port 8081"
