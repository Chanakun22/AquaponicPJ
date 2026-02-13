#!/usr/bin/env python3
"""
Aquaponics Camera MJPEG HTTP Server
Uses picamera2 to serve a proper MJPEG stream over HTTP.
Replaces rpicam-vid TCP mode which crashes on Trixie.
"""

import io
import time
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
from urllib.parse import urlparse
from picamera2 import Picamera2
from picamera2.encoders import MJPEGEncoder
from picamera2.outputs import FileOutput

# --- Configuration (loaded from settings.json) ---
import json
import os

SETTINGS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings.json")

def load_camera_settings():
    """Load camera settings from settings.json, with defaults."""
    defaults = {"width": 1280, "height": 720, "framerate": 15}
    try:
        if os.path.exists(SETTINGS_FILE):
            with open(SETTINGS_FILE, "r") as f:
                settings = json.load(f)
                cam = settings.get("camera", {})
                return {
                    "width": cam.get("width", defaults["width"]),
                    "height": cam.get("height", defaults["height"]),
                    "framerate": cam.get("framerate", defaults["framerate"]),
                }
    except Exception as e:
        print(f"⚠️ Could not load settings: {e}, using defaults")
    return defaults

cam_settings = load_camera_settings()
PORT = 8081
WIDTH = cam_settings["width"]
HEIGHT = cam_settings["height"]
FRAMERATE = cam_settings["framerate"]

class StreamingOutput(io.BufferedIOBase):
    """Thread-safe output buffer for MJPEG frames."""
    def __init__(self):
        self.frame = None
        self.condition = threading.Condition()

    def write(self, buf):
        with self.condition:
            self.frame = buf
            self.condition.notify_all()
        return len(buf)

class StreamingHandler(BaseHTTPRequestHandler):
    """HTTP handler that serves MJPEG stream."""
    output = None  # Set by main()

    def do_GET(self):
        # Strip query string so /stream?t=123 matches /stream
        path = urlparse(self.path).path
        if path in ('/', '/stream', '/stream.mjpg', '/video'):
            self.send_response(200)
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=FRAME')
            self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            try:
                while True:
                    with self.output.condition:
                        self.output.condition.wait()
                        frame = self.output.frame
                    self.wfile.write(b'--FRAME\r\n')
                    self.wfile.write(b'Content-Type: image/jpeg\r\n')
                    self.wfile.write(f'Content-Length: {len(frame)}\r\n'.encode())
                    self.wfile.write(b'\r\n')
                    self.wfile.write(frame)
                    self.wfile.write(b'\r\n')
            except (BrokenPipeError, ConnectionResetError):
                # Client disconnected - this is normal
                pass
        elif path == '/snapshot':
            # Single JPEG snapshot
            with self.output.condition:
                self.output.condition.wait()
                frame = self.output.frame
            self.send_response(200)
            self.send_header('Content-Type', 'image/jpeg')
            self.send_header('Content-Length', str(len(frame)))
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(frame)
        else:
            self.send_error(404)

    def log_message(self, format, *args):
        # Suppress per-request logging to keep output clean
        pass

class StreamingServer(ThreadingMixIn, HTTPServer):
    allow_reuse_address = True
    daemon_threads = True

def main():
    print("=========================================")
    print("  📸 Aquaponics Camera Stream (HTTP)")
    print(f"  Resolution: {WIDTH}x{HEIGHT} @ {FRAMERATE}fps")
    print(f"  Port: {PORT}")
    print("=========================================")

    # Initialize camera
    picam2 = Picamera2()
    config = picam2.create_video_configuration(
        main={"size": (WIDTH, HEIGHT), "format": "RGB888"},
        controls={"FrameRate": FRAMERATE}
    )
    picam2.configure(config)

    # Setup streaming output
    output = StreamingOutput()
    StreamingHandler.output = output

    # Start camera with MJPEG encoder
    encoder = MJPEGEncoder()
    picam2.start_recording(encoder, FileOutput(output))
    print(f"[Camera] Started - imx219 @ {WIDTH}x{HEIGHT}")

    # Start HTTP server
    server = StreamingServer(('0.0.0.0', PORT), StreamingHandler)
    print(f"[Server] Listening on http://0.0.0.0:{PORT}")
    print(f"[Server] Stream: http://0.0.0.0:{PORT}/stream")
    print(f"[Server] Snapshot: http://0.0.0.0:{PORT}/snapshot")
    print("-----------------------------------------")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        print("\n[Camera] Stopping...")
        picam2.stop_recording()
        server.shutdown()
        print("[Camera] Stopped.")

if __name__ == '__main__':
    main()
