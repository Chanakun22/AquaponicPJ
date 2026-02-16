#!/usr/bin/env python3
"""
Aquaponics Camera MJPEG HTTP Server (On-Demand)
Uses picamera2 to serve a proper MJPEG stream over HTTP.
Camera only encodes when viewers are connected to save CPU.
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
    defaults = {"width": 1280, "height": 720, "framerate": 15, "quality": 80}
    try:
        if os.path.exists(SETTINGS_FILE):
            with open(SETTINGS_FILE, "r") as f:
                settings = json.load(f)
                cam = settings.get("camera", {})
                return {
                    "width": cam.get("width", defaults["width"]),
                    "height": cam.get("height", defaults["height"]),
                    "framerate": cam.get("framerate", defaults["framerate"]),
                    "quality": cam.get("quality", defaults["quality"]),
                }
    except Exception as e:
        print(f"⚠️ Could not load settings: {e}, using defaults")
    return defaults

cam_settings = load_camera_settings()
PORT = 8081
WIDTH = cam_settings["width"]
HEIGHT = cam_settings["height"]
FRAMERATE = cam_settings["framerate"]
QUALITY = max(1, min(100, cam_settings["quality"]))  # Clamp 1-100


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


class CameraManager:
    """Manages camera start/stop based on active viewer count.
    Camera hardware is fully OFF when no viewers — zero CPU usage."""
    def __init__(self, output):
        self.output = output
        self.lock = threading.Lock()
        self.viewer_count = 0
        self.running = False
        self.picam2 = None
        self.encoder = None
        print(f"[Camera] Ready - {WIDTH}x{HEIGHT} @ {FRAMERATE}fps q={QUALITY}")
        print(f"[Camera] 💤 Camera OFF until viewer connects")

    def _start_camera(self):
        """Start camera + encoder (called when first viewer connects)."""
        try:
            self.picam2 = Picamera2()
            config = self.picam2.create_video_configuration(
                main={"size": (WIDTH, HEIGHT), "format": "RGB888"},
                controls={
                    "FrameRate": FRAMERATE,
                    "AwbEnable": True,
                    "AeEnable": True,
                }
            )
            self.picam2.configure(config)
            self.encoder = MJPEGEncoder()
            self.encoder.quality = QUALITY
            self.picam2.start_recording(self.encoder, FileOutput(self.output))
            self.running = True
            print("[Camera] ▶ Camera ON (viewer connected)")
        except Exception as e:
            print(f"[Camera] ⚠️ Failed to start camera: {e}")

    def _stop_camera(self):
        """Stop camera + encoder completely (called when last viewer leaves)."""
        try:
            if self.picam2:
                self.picam2.stop_recording()
                self.picam2.stop()
                self.picam2.close()
                self.picam2 = None
                self.encoder = None
            self.running = False
            print("[Camera] ⏸ Camera OFF (no viewers)")
        except Exception as e:
            print(f"[Camera] ⚠️ Failed to stop camera: {e}")

    def add_viewer(self):
        """Called when a new viewer connects."""
        with self.lock:
            self.viewer_count += 1
            count = self.viewer_count
            if not self.running:
                self._start_camera()
        print(f"[Camera] Viewer connected ({count} active)")

    def remove_viewer(self):
        """Called when a viewer disconnects."""
        with self.lock:
            self.viewer_count = max(0, self.viewer_count - 1)
            count = self.viewer_count
            if count == 0 and self.running:
                self._stop_camera()
        print(f"[Camera] Viewer disconnected ({count} active)")

    def snapshot(self):
        """Take a single snapshot (temporarily starts camera if needed)."""
        with self.lock:
            was_running = self.running
            if not self.running:
                self._start_camera()

        # Wait for a frame
        with self.output.condition:
            self.output.condition.wait(timeout=3)
            frame = self.output.frame

        # Stop if we started just for snapshot and no viewers
        with self.lock:
            if not was_running and self.viewer_count == 0:
                self._stop_camera()

        return frame

    def shutdown(self):
        """Clean shutdown."""
        with self.lock:
            if self.running:
                self._stop_camera()
        print("[Camera] Shutdown complete.")


class StreamingHandler(BaseHTTPRequestHandler):
    """HTTP handler that serves MJPEG stream."""
    output = None      # Set by main()
    cam_manager = None  # Set by main()

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ('/', '/stream', '/stream.mjpg', '/video'):
            # Register viewer
            self.cam_manager.add_viewer()

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
                pass
            finally:
                # Unregister viewer when they disconnect
                self.cam_manager.remove_viewer()

        elif path == '/snapshot':
            frame = self.cam_manager.snapshot()
            if frame:
                self.send_response(200)
                self.send_header('Content-Type', 'image/jpeg')
                self.send_header('Content-Length', str(len(frame)))
                self.send_header('Access-Control-Allow-Origin', '*')
                self.end_headers()
                self.wfile.write(frame)
            else:
                self.send_error(503, 'No frame available')
        else:
            self.send_error(404)

    def log_message(self, format, *args):
        pass

class StreamingServer(ThreadingMixIn, HTTPServer):
    allow_reuse_address = True
    daemon_threads = True

def main():
    print("=========================================")
    print("  📸 Aquaponics Camera Stream (On-Demand)")
    print(f"  Resolution: {WIDTH}x{HEIGHT} @ {FRAMERATE}fps")
    print(f"  JPEG Quality: {QUALITY}%")
    print(f"  Port: {PORT}")
    print("=========================================")

    # Setup streaming output
    output = StreamingOutput()

    # Initialize camera manager (on-demand start/stop)
    cam_manager = CameraManager(output)

    # Wire up handler
    StreamingHandler.output = output
    StreamingHandler.cam_manager = cam_manager

    # Start HTTP server
    server = StreamingServer(('0.0.0.0', PORT), StreamingHandler)
    print(f"[Server] Listening on http://0.0.0.0:{PORT}")
    print(f"[Server] Stream: http://0.0.0.0:{PORT}/stream")
    print(f"[Server] Snapshot: http://0.0.0.0:{PORT}/snapshot")
    print(f"[Server] 💤 Camera idle until viewer connects")
    print("-----------------------------------------")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        print("\n[Camera] Shutting down...")
        cam_manager.shutdown()
        server.shutdown()

if __name__ == '__main__':
    main()
