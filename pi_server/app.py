from flask import Flask, jsonify, send_file, request
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt
import json
import threading
import psutil
import time
from datetime import datetime, timedelta
import os

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# === Settings File ===
SETTINGS_FILE = "settings.json"

def load_settings():
    """Load settings from JSON file"""
    default = {
        "thresholds": {
            "ph_min": 6.0, "ph_max": 7.5,
            "water_temp_min": 22, "water_temp_max": 30,
            "air_temp_min": 25, "air_temp_max": 35,
            "humidity_min": 50, "humidity_max": 80,
            "tds_min": 300, "tds_max": 600
        },
        "notifications": {
            "line_token": "",
            "alert_cooldown_min": 15,
            "enabled": False
        },
        "display": {
            "graph_days": 3,
            "device_name": "Aquaponics System"
        },
        "sensor_config": {
            "tds": True,
            "ph": True,
            "water": True,
            "air": True,
            "light": True
        },
        "tds_calibration": {
            "low_ppm": 500,
            "low_voltage": 0.0,
            "high_ppm": 1000,
            "high_voltage": 0.0,
            "calibrated": False
        },
        "ph_calibration": {
            "cal7_done": False,
            "cal4_done": False,
            "last_voltage": 0.0,
            "last_ph": 0.0
        },
        "camera": {
            "width": 1280,
            "height": 720,
            "framerate": 15,
            "quality": 80
        }
    }
    try:
        if os.path.exists(SETTINGS_FILE):
            with open(SETTINGS_FILE, "r") as f:
                return json.load(f)
    except Exception as e:
        print(f"Error loading settings: {e}")
    return default



# Load settings on startup
app_settings = load_settings()

# === Line Notify ===
import requests

# Track last alert time per sensor to implement cooldown
_last_alert_time = {}

def send_line_notify(message):
    """Send notification via Line Notify"""
    global app_settings
    token = app_settings.get("notifications", {}).get("line_token", "")
    if not token:
        print("⚠️ Line Notify: No token configured")
        return False
    
    try:
        # Use LINE Messaging API (Broadcast)
        url = "https://api.line.me/v2/bot/message/broadcast"
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {token}"
        }
        payload = {
            "messages": [
                {
                    "type": "text",
                    "text": message
                }
            ]
        }
        
        response = requests.post(url, headers=headers, json=payload, timeout=5)
        
        if response.status_code == 200:
            print(f"✅ LINE Alert sent: {message}")
            save_log(f"📱 LINE Alert: {message}")
            return True
        else:
            print(f"❌ LINE Alert failed: {response.status_code} - {response.text}")
            return False
    except Exception as e:
        print(f"❌ LINE Error: {e}")
        return False

def check_thresholds(data):
    """Check sensor data against thresholds and send alerts"""
    global app_settings, _last_alert_time
    
    # Check if notifications are enabled
    notifications = app_settings.get("notifications", {})
    if not notifications.get("enabled", False):
        return
    
    thresholds = app_settings.get("thresholds", {})
    cooldown_min = notifications.get("alert_cooldown_min", 15)
    now = time.time()
    
    alerts = []
    
    # Check each sensor
    checks = [
        ("ph", data.get("ph", 0), thresholds.get("ph_min", 0), thresholds.get("ph_max", 14), "pH"),
        ("water_temp", data.get("water_temp", 0), thresholds.get("water_temp_min", 0), thresholds.get("water_temp_max", 100), "อุณหภูมิน้ำ"),
        ("air_temp", data.get("air_temp", 0), thresholds.get("air_temp_min", 0), thresholds.get("air_temp_max", 100), "อุณหภูมิอากาศ"),
        ("humidity", data.get("humidity", 0), thresholds.get("humidity_min", 0), thresholds.get("humidity_max", 100), "ความชื้น"),
        ("tds", data.get("tds", 0), thresholds.get("tds_min", 0), thresholds.get("tds_max", 2000), "TDS"),
    ]
    
    for key, value, min_val, max_val, name in checks:
        if value is None or value == 0:
            continue
        
        # Check if out of range
        if value < min_val or value > max_val:
            # Check cooldown
            last_time = _last_alert_time.get(key, 0)
            if now - last_time > cooldown_min * 60:
                if value < min_val:
                    alerts.append(f"⚠️ {name} ต่ำเกินไป: {value} (ต่ำกว่า {min_val})")
                else:
                    alerts.append(f"⚠️ {name} สูงเกินไป: {value} (สูงกว่า {max_val})")
                _last_alert_time[key] = now
    
    # Send alerts if any
    if alerts:
        message = "\n🌿 Aquaponics Alert!\n" + "\n".join(alerts)
        send_line_notify(message)

# เก็บข้อมูลล่าสุดไว้ในตัวแปร Memory
last_data = {
    # ESP32 Data
    "water_temp": 0, "air_temp": 0, "humidity": 0,
    "tds": 0, "ph": 0, "light": 0,
    "uptime_sec": 0, "wifi_rssi": 0, "free_heap": 0,
    
    # Pi Server Data
    "pi_cpu_percent": 0,
    "pi_ram_percent": 0,
    "pi_ram_total": 0,
    "pi_ram_used": 0,
    "pi_temp": 0
}

# === System Monitor Function ===
def get_pi_temp():
    try:
        with open("/sys/class/thermal/thermal_zone0/temp", "r") as f:
            temp = float(f.read()) / 1000
            return temp
    except:
        return 0

# === Database Setup (SQLite) ===
import sqlite3

DB_FILE = "aquaponics.db"
db_lock = threading.Lock()  # ป้องกัน concurrent write จาก MQTT Thread + Web Thread

def init_db():
    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            
            # Sensor Data Table
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS sensors (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                    water_temp REAL,
                    air_temp REAL,
                    humidity REAL,
                    tds REAL,
                    ph REAL,
                    light REAL
                )
            ''')
            
            # Settings History Table (New)
            cursor.execute('''
                CREATE TABLE IF NOT EXISTS settings_history (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                    settings_json TEXT
                )
            ''')
            
            conn.commit()
            print("✅ Database initialized")
        except Exception as e:
            print(f"❌ Database Error: {e}")
        finally:
            if conn:
                conn.close()

# Initialize DB on start
init_db()

def save_settings_to_db(settings):
    """Save settings snapshot to database"""
    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            cursor.execute('INSERT INTO settings_history (settings_json) VALUES (?)', (json.dumps(settings),))
            conn.commit()
            # print("💾 Settings saved to DB history")
        except Exception as e:
            print(f"❌ Failed to save settings to DB: {e}")
        finally:
            if conn:
                conn.close()

def save_settings(settings):
    """Save settings to JSON file atomically and Database"""
    try:
        # 1. Save to File (Actual Config) - Atomic Write
        tmp_file = f"{SETTINGS_FILE}.tmp"
        with open(tmp_file, "w") as f:
            json.dump(settings, f, indent=2)
            f.flush()
            os.fsync(f.fileno()) # Ensure data is written to disk
        
        # Atomically replace the old file with the new one
        os.replace(tmp_file, SETTINGS_FILE)
            
        # 2. Save to DB (History/Backup)
        save_settings_to_db(settings)
        
        return True
    except Exception as e:
        print(f"Error saving settings: {e}")
        # Clean up tmp file if error occurred
        if os.path.exists(f"{SETTINGS_FILE}.tmp"):
            try:
                os.remove(f"{SETTINGS_FILE}.tmp")
            except:
                pass
        return False

# === Log Storage (In-Memory + File) ===
from collections import deque
LOG_FILE = "system.log"
log_buffer = deque(maxlen=50)  # Keep last 50 logs in memory for web

def save_log(msg):
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    log_entry = f"[{timestamp}] {msg}"
    
    # Add to memory
    log_buffer.appendleft(log_entry)
    
    # Add to file
    try:
        with open(LOG_FILE, "a") as f:
            f.write(log_entry + "\n")
    except Exception as e:
        print(f"Error saving log: {e}")

# === MQTT Functions ===
def on_connect(client, userdata, flags, rc):
    print(f"✅ MQTT Connected with result code {rc}")
    client.subscribe("aquaponics/sensors")
    client.subscribe("aquaponics/logs")
    client.subscribe("aquaponics/status/sensors")
    client.subscribe("aquaponics/status/ph_cal")


def on_message(client, userdata, msg):
    global last_data, last_esp_update, esp_online, app_settings
    try:
        topic = msg.topic
        payload = msg.payload.decode()
        
        if topic == "aquaponics/logs":
            print(f"📝 Log: {payload}")
            save_log(payload)
            return
            
        if topic == "aquaponics/status/sensors":
            try:
                new_config = json.loads(payload)
                # Update local settings if different
                if app_settings.get("sensor_config") != new_config:
                    app_settings["sensor_config"] = new_config
                    save_settings(app_settings)
                    print(f"🔄 Synced Sensor Config from ESP32: {new_config}")
            except Exception as e:
                print(f"❌ Error syncing sensor config: {e}")
            return
        
        if topic == "aquaponics/status/ph_cal":
            try:
                ph_status = json.loads(payload)
                app_settings.setdefault("ph_calibration", {})
                app_settings["ph_calibration"]["last_voltage"] = ph_status.get("ph_voltage", 0)
                app_settings["ph_calibration"]["last_ph"] = ph_status.get("ph_value", 0)
                if ph_status.get("calibrated"):
                    app_settings["ph_calibration"]["cal7_done"] = True
                save_settings(app_settings)
                print(f"🔄 pH Calibration status updated: {ph_status}")
                save_log(f"pH Calibration updated: voltage={ph_status.get('ph_voltage')}mV, pH={ph_status.get('ph_value')}")
            except Exception as e:
                print(f"❌ Error handling pH cal status: {e}")
            return
        


        # It's sensor data
        # Update heartbeat
        now = time.time()
        last_esp_update = now
        
        # If was offline, mark online and log
        if not esp_online:
            esp_online = True
            save_log("✅ ESP32 Reconnected!")
        
        data = json.loads(payload)
        for key in data:
            last_data[key] = data[key]
        
        # If a sensor key is MISSING from payload, ESP32 read NaN → mark as failed
        SENSOR_KEYS = ["water_temp", "air_temp", "humidity", "tds", "ph", "light"]
        for sk in SENSOR_KEYS:
            if sk not in data:
                last_data[sk] = None
            
        # === Save to DB (Filtered) ===
        # Save only every 60 seconds to save space
        save_data_to_db(data)
        
        # === Check Thresholds & Send Alerts ===
        check_thresholds(data)
            
    except Exception as e:
        print(f"❌ Error parsing MQTT: {e}")

# === DB Throttling ===
last_db_save = 0

def save_data_to_db(data):
    global last_db_save
    now = time.time()
    
    if now - last_db_save < 60: # 60 seconds interval
        return

    with db_lock:
        conn = None
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO sensors (water_temp, air_temp, humidity, tds, ph, light)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (
                data.get("water_temp", 0),
                data.get("air_temp", 0),
                data.get("humidity", 0),
                data.get("tds", 0),
                data.get("ph", 0),
                data.get("light", 0)
            ))
            conn.commit()
            last_db_save = now
            # print("💾 Data saved to DB") 
        except Exception as e:
            print(f"DB Insert Error: {e}")
        finally:
            if conn:
                conn.close()

# === Start MQTT in Background Thread ===
mqtt_client = None  # Global reference for publishing



def start_mqtt():
    global mqtt_client
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    try:
        # "localhost" เพราะ Mosquitto อยู่เครื่องเดียวกับ app.py
        mqtt_client.connect("localhost", 1883, 60)
        mqtt_client.loop_forever()
    except Exception as e:
        print(f"❌ Could not connect to MQTT Broker: {e}")
        save_log(f"MQTT Error: {e}")

# === Web Server Routes ===
@app.route('/base.css')
def serve_base_css():
    return send_file('base.css')

@app.route('/header.js')
def serve_header_js():
    return send_file('header.js')

@app.route('/')
def index():
    return send_file('index.html')

@app.route('/graphs')
def graphs_page():
    return send_file('graphs.html')

@app.route('/full_logs')
def full_logs_page():
    return send_file('full_logs.html')

@app.route('/api/sensors')
def get_sensors():
    return jsonify(last_data)

@app.route('/settings')
def settings_page():
    return send_file('settings.html')

@app.route('/pwa/<path:filename>')
def serve_pwa(filename):
    return send_file(f'pwa/{filename}')

@app.route('/live')
def live_page():
    return send_file('live.html')

@app.route('/cam-stream')
def cam_stream():
    """Proxy the camera stream from localhost:8081 for remote access"""
    try:
        from flask import Response
        import requests
        req = requests.get('http://127.0.0.1:8081/stream', stream=True, timeout=5)
        return Response(req.iter_content(chunk_size=5120), content_type=req.headers.get('Content-Type', 'multipart/x-mixed-replace; boundary=frame'))
    except Exception as e:
        print(f"❌ Camera Proxy Error: {e}")
        return "Camera stream not available locally (port 8081)", 502

@app.route('/api/settings', methods=['GET'])
def get_settings():
    global app_settings
    return jsonify(app_settings)

@app.route('/api/settings', methods=['POST'])
def post_settings():
    global app_settings
    try:
        new_settings = request.get_json()
        if new_settings:
            app_settings = new_settings
            # Check if sensor config changed and publish to MQTT
            if "sensor_config" in new_settings:
                try:
                    sensor_payload = json.dumps(new_settings["sensor_config"])
                    if mqtt_client and mqtt_client.is_connected():
                         # Note: Client connection check might fail if threaded, but try best effort
                         mqtt_client.publish("aquaponics/config/sensors", sensor_payload, qos=1, retain=True)
                         print(f"📤 Sensor Config sent to ESP32: {sensor_payload}")
                except Exception as ex:
                    print(f"MQTT Publish Error: {ex}")

            if save_settings(app_settings):

                return jsonify({"status": "ok", "message": "Settings saved"})
            else:
                return jsonify({"status": "error", "message": "Failed to save"}), 500
        return jsonify({"status": "error", "message": "Invalid data"}), 400
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === TDS Calibration API ===
@app.route('/api/tds_voltage')
def get_tds_voltage():
    """Get current TDS voltage from ESP32 sensor data"""
    voltage = last_data.get("tds_voltage", 0)
    return jsonify({"voltage": voltage})

@app.route('/api/tds_calibrate', methods=['POST'])
def post_tds_calibrate():
    """Save TDS calibration and send to ESP32 via MQTT"""
    global app_settings, mqtt_client
    try:
        data = request.get_json()
        low_temp = float(data.get("low_temp", 25.0))
        low_ppm = float(data.get("low_ppm", 0))
        low_voltage = float(data.get("low_voltage", 0))
        high_temp = float(data.get("high_temp", 25.0))
        high_ppm = float(data.get("high_ppm", 0))
        high_voltage = float(data.get("high_voltage", 0))
        
        # Validate
        if low_voltage <= 0 or high_voltage <= 0:
            return jsonify({"status": "error", "message": "Invalid voltage values"}), 400
        if low_voltage == high_voltage:
            return jsonify({"status": "error", "message": "Low and High voltage cannot be the same"}), 400
            
        # Save to settings
        app_settings["tds_calibration"] = {
            "low_temp": low_temp,
            "low_ppm": low_ppm,
            "low_voltage": low_voltage,
            "high_temp": high_temp,
            "high_ppm": high_ppm,
            "high_voltage": high_voltage,
            "calibrated": True
        }
        save_settings(app_settings)
        
        # Publish to ESP32 via MQTT
        if mqtt_client and mqtt_client.is_connected():
            import json
            payload = json.dumps({
                "low_ppm": low_ppm,
                "low_voltage": low_voltage,
                "high_ppm": high_ppm,
                "high_voltage": high_voltage
            })
            mqtt_client.publish("aquaponics/config/tds_cal", payload)
            print(f"📤 TDS Calibration sent to ESP32: {payload}")
            save_log(f"TDS Calibration updated: Low={low_ppm}ppm, High={high_ppm}ppm")
        
        return jsonify({"status": "ok", "message": "TDS Calibration saved and sent to ESP32"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === pH Calibration API ===
@app.route('/api/ph_voltage')
def get_ph_voltage():
    """Get current pH voltage and value from ESP32 sensor data"""
    voltage = last_data.get("ph_voltage", 0)
    ph_value = last_data.get("ph_value", 0)
    return jsonify({"voltage": voltage, "ph_value": ph_value})

@app.route('/api/ph_calibrate', methods=['POST'])
def post_ph_calibrate():
    """Send pH calibration command to ESP32 via MQTT"""
    global app_settings, mqtt_client
    try:
        data = request.get_json()
        action = data.get("action", "")
        
        if action not in ["cal7", "cal4", "clear"]:
            return jsonify({"status": "error", "message": "Invalid action. Use cal7, cal4, or clear"}), 400
        
        # Publish to ESP32 via MQTT
        if mqtt_client and mqtt_client.is_connected():
            payload = json.dumps({"action": action})
            mqtt_client.publish("aquaponics/config/ph_cal", payload, qos=1)
            print(f"📤 pH Calibration command sent to ESP32: {action}")
            save_log(f"pH Calibration triggered: {action}")
            
            # Update local settings
            app_settings.setdefault("ph_calibration", {})
            if action == "cal7":
                app_settings["ph_calibration"]["cal7_done"] = True
            elif action == "cal4":
                app_settings["ph_calibration"]["cal4_done"] = True
            elif action == "clear":
                app_settings["ph_calibration"] = {
                    "cal7_done": False, "cal4_done": False,
                    "last_voltage": 0, "last_ph": 0
                }
            save_settings(app_settings)
            
            return jsonify({"status": "ok", "message": f"pH {action} command sent to ESP32"})
        else:
            return jsonify({"status": "error", "message": "MQTT not connected"}), 503
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === Heartbeat Monitor ===
last_esp_update = 0
esp_online = False

def monitor_heartbeat():
    global last_esp_update, esp_online
    while True:
        try:
            now = time.time()
            # If no data for 15 seconds, mark as offline
            if esp_online and (now - last_esp_update > 15):
                esp_online = False
                msg = "⚠️ ALERT: ESP32 Connection Lost!"
                print(msg)
                save_log(msg)
            
            time.sleep(5) # Check every 5 seconds
        except Exception as e:
            print(f"Monitor Error: {e}")
            time.sleep(5)

@app.route('/api/health')
def get_health():
    # Update Real-time Pi Stats
    last_data["pi_cpu_percent"] = psutil.cpu_percent()
    mem = psutil.virtual_memory()
    last_data["pi_ram_total"] = mem.total
    last_data["pi_ram_used"] = mem.used
    last_data["pi_ram_percent"] = mem.percent
    last_data["pi_temp"] = get_pi_temp()
    
    # Add Heartbeat Status
    last_data["esp_status"] = "ONLINE" if esp_online else "OFFLINE"
    last_data["last_seen_sec"] = int(time.time() - last_esp_update) if last_esp_update > 0 else -1
    
    return jsonify(last_data) 

@app.route('/api/info')
def get_info():
    return jsonify({"firmware": "Pi-Server-v2 (Monitoring)", "status": "online"})

@app.route('/api/health/details')
def get_health_details():
    """Check Pi services and ESP32 sensors status"""
    import subprocess, time as _time

    result = {"services": [], "sensors": []}

    # === Pi Services ===
    services = [
        ("aquaponics", "Web Server"),
        ("aquaponics-hotspot", "Hotspot (AP)"),
        ("aquaponics-cam", "Camera"),
        ("dnsmasq", "DHCP Server"),
        ("mosquitto", "MQTT Broker"),
        ("tailscaled", "Tailscale VPN"),
    ]

    for svc_name, display_name in services:
        try:
            out = subprocess.run(
                ['systemctl', 'is-active', svc_name],
                capture_output=True, text=True, timeout=3
            )
            status = out.stdout.strip()
            ok = status == 'active'

            # Camera special case: app.py starts cam_server directly,
            # so systemd may show 'activating' while camera works fine
            if not ok and svc_name == 'aquaponics-cam':
                proc = subprocess.run(
                    ['pgrep', '-f', 'cam_server'],
                    capture_output=True, timeout=3
                )
                if proc.returncode == 0:
                    ok = True
                    status = 'running'
        except Exception:
            status = 'error'
            ok = False

        result["services"].append({
            "name": display_name,
            "service": svc_name,
            "status": status,
            "ok": ok
        })

    # === ESP32 Connection ===
    esp_age = int(_time.time() - last_esp_update) if last_esp_update > 0 else -1
    result["esp_online"] = esp_online
    result["esp_last_seen"] = esp_age

    # === ESP32 Sensors ===
    # (key, name, unit, min_val, max_val, error_values)
    sensor_checks = [
        ("water_temp", "Water Temp", "°C", 5, 50, [85.0, -127, 0]),
        ("air_temp", "Air Temp", "°C", 5, 60, [0]),
        ("humidity", "Humidity", "%", 10, 100, [0]),
        ("tds", "TDS", "ppm", 1, 5000, []),
        ("ph", "pH", "", 1, 14, [0]),
        ("light", "Light", "lux", 0, 100000, []),
    ]

    for key, name, unit, min_val, max_val, error_vals in sensor_checks:
        value = last_data.get(key, None)
        # Sensor OK if: ESP online, value in range, AND not a known error value
        ok = (esp_online
              and value is not None
              and isinstance(value, (int, float))
              and min_val <= value <= max_val
              and value not in error_vals)
        result["sensors"].append({
            "name": name,
            "key": key,
            "value": value if value is not None else "N/A",
            "unit": unit if value is not None else "",
            "ok": ok
        })

    return jsonify(result)

@app.route('/api/logs')
def get_logs():
    return jsonify(list(log_buffer))

@app.route('/logs_view')
def logs_view():
    return send_file('full_logs.html')

@app.route('/graphs_view')
def graphs_view():
    return send_file('graphs.html')

# === Camera Settings API ===
@app.route('/api/camera_restart', methods=['POST'])
def restart_camera():
    """Save camera settings and restart cam_server.py"""
    global app_settings
    try:
        data = request.get_json()
        width = int(data.get('width', 1280))
        height = int(data.get('height', 720))
        framerate = int(data.get('framerate', 15))
        quality = int(data.get('quality', 80))
        
        # Validate
        valid_resolutions = [(640, 480), (1280, 720), (1920, 1080)]
        if (width, height) not in valid_resolutions:
            return jsonify({'status': 'error', 'message': 'Invalid resolution'}), 400
        if framerate < 1 or framerate > 30:
            return jsonify({'status': 'error', 'message': 'FPS must be 1-30'}), 400
        if quality < 1 or quality > 100:
            return jsonify({'status': 'error', 'message': 'Quality must be 1-100'}), 400
        
        # Save to settings
        app_settings['camera'] = {
            'width': width,
            'height': height,
            'framerate': framerate,
            'quality': quality
        }
        save_settings(app_settings)
        save_log(f"📷 Camera settings updated: {width}x{height} @ {framerate}fps q={quality}")
        
        # Restart camera server
        import subprocess
        try:
            subprocess.run(['sudo', 'pkill', '-f', 'cam_server.py'], timeout=5)
            import time as _time
            _time.sleep(1)
            cam_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'start_cam.sh')
            subprocess.Popen(['bash', cam_script])
            print(f"📷 Camera restarted: {width}x{height} @ {framerate}fps q={quality}")
        except Exception as ex:
            print(f"⚠️ Camera restart error: {ex}")
        
        return jsonify({'status': 'ok', 'message': f'Camera restarting with {width}x{height} @ {framerate}fps q={quality}'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/full_logs_file')
def get_full_logs_file():
    try:
        with open(LOG_FILE, "r") as f:
            return f.read()
    except:
        return "No logs found."

@app.route('/api/clear_logs', methods=['POST'])
def clear_logs_file():
    try:
        # Clear the file
        open(LOG_FILE, 'w').close()
        # Clear memory buffer too
        log_buffer.clear()
        return jsonify({"status": "success", "message": "Logs cleared"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/history')
def get_history():
    global app_settings
    try:
        with db_lock:
            conn = None
            try:
                conn = sqlite3.connect(DB_FILE)
                cursor = conn.cursor()
                
                # Read graph_days from settings (default 3)
                days = app_settings.get("display", {}).get("graph_days", 3)
                limit = days * 24 * 60  # 1 record per minute
                
                cursor.execute('''
                    SELECT timestamp, water_temp, air_temp, humidity, tds, ph, light
                    FROM sensors 
                    ORDER BY id DESC 
                    LIMIT ?
                ''', (limit,))
                rows = cursor.fetchall()
            finally:
                if conn:
                    conn.close()
        
        # Reverse to chronological order (oldest first)
        rows.reverse()
        
        # Format for Chart.js
        labels = []
        data = {
            "water_temp": [],
            "air_temp": [],
            "humidity": [],
            "tds": [],
            "ph": [],
            "light": []
        }
        
        for r in rows:
            # timestamp is r[0] e.g. "2023-10-27 10:00:00"
            # SQLite stores as UTC. Convert directly to Thai Time (UTC+7)
            try:
                # Parse string to datetime
                dt_utc = datetime.strptime(r[0], "%Y-%m-%d %H:%M:%S")
                # Add 7 hours
                dt_thai = dt_utc + timedelta(hours=7)
                # Format specific for graph label (HH:MM) or include Date if needed
                # User wants 3 days, so maybe show date if it's a new day? 
                # For now let's just do HH:MM and maybe Date/Time if tooltip? 
                # ChartJS labels are usually X axis.
                # Let's use simple HH:MM first as requested.
                t_str = dt_thai.strftime("%d/%m %H:%M") # dd/mm HH:MM is better for 3 days
            except:
                t_str = r[0] # Fallback
            
            labels.append(t_str)
            data["water_temp"].append(r[1])
            data["air_temp"].append(r[2])
            data["humidity"].append(r[3])
            data["tds"].append(r[4])
            data["ph"].append(r[5])
            data["light"].append(r[6])
            
        return jsonify({"labels": labels, "datasets": data})

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# === WebSocket Support ===
def build_dashboard_data():
    """Build combined data payload for WebSocket broadcast"""
    # Update Pi stats
    last_data["pi_cpu_percent"] = psutil.cpu_percent()
    mem = psutil.virtual_memory()
    last_data["pi_ram_total"] = mem.total
    last_data["pi_ram_used"] = mem.used
    last_data["pi_ram_percent"] = mem.percent
    last_data["pi_temp"] = get_pi_temp()
    last_data["esp_status"] = "ONLINE" if esp_online else "OFFLINE"
    last_data["last_seen_sec"] = int(time.time() - last_esp_update) if last_esp_update > 0 else -1
    
    return {
        "sensors": dict(last_data),
        "health": dict(last_data),
        "info": {"firmware": "Pi-Server-v2 (Monitoring)", "status": "online"},
        "logs": list(log_buffer)
    }

def ws_broadcast():
    """Background thread: broadcast dashboard data to all WebSocket clients"""
    print("📡 WebSocket broadcast thread started")
    while True:
        socketio.sleep(2)
        try:
            data = build_dashboard_data()
            socketio.emit('dashboard_update', data)
        except Exception as e:
            print(f"WS Broadcast Error: {e}")

@socketio.on('connect')
def handle_ws_connect():
    """Send initial data when a client connects via WebSocket"""
    print("🔌 WebSocket client connected")
    try:
        data = build_dashboard_data()
        emit('dashboard_update', data)
    except Exception as e:
        print(f"WS Initial send error: {e}")

@socketio.on('disconnect')
def handle_ws_disconnect():
    print("🔌 WebSocket client disconnected")

# === OTA Upload via Web ===
import uuid
import tempfile

ota_tasks = {}

@app.route('/ota')
def ota_page():
    return send_file('ota.html')

@app.route('/api/ota/upload', methods=['POST'])
def ota_upload():
    """Receive firmware.bin from browser, then flash to ESP32 via espota.py"""
    try:
        if 'firmware' not in request.files:
            return jsonify({"status": "error", "message": "ไม่มีไฟล์ firmware"}), 400

        firmware = request.files['firmware']
        if not firmware.filename.endswith('.bin'):
            return jsonify({"status": "error", "message": "กรุณาใช้ไฟล์ .bin เท่านั้น"}), 400

        firmware_path = os.path.join(tempfile.gettempdir(), 'firmware_ota.bin')
        firmware.save(firmware_path)
        file_size = os.path.getsize(firmware_path)

        print(f"📦 OTA: Received firmware {firmware.filename} ({file_size} bytes)")
        save_log(f"📦 OTA Upload: {firmware.filename} ({file_size} bytes)")

        task_id = str(uuid.uuid4())[:8]
        ota_tasks[task_id] = {
            "status": "flashing",
            "message": "",
            "logs": [f"📦 Firmware received: {file_size} bytes",
                     "🔄 Starting espota flash to ESP32..."]
        }

        esp_ip = "192.168.10.10"
        ota_password = "admin123"
        espota_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'espota.py')

        def run_ota():
            try:
                cmd = [
                    'python3', espota_script,
                    '-i', esp_ip, '-p', '3232',
                    '--auth=' + ota_password,
                    '-f', firmware_path, '-d', '-r'
                ]
                ota_tasks[task_id]["logs"].append(f"🚀 Flashing to {esp_ip}:3232...")
                print(f"🚀 OTA Command: {' '.join(cmd)}")

                result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)

                if result.returncode == 0:
                    ota_tasks[task_id]["status"] = "success"
                    ota_tasks[task_id]["message"] = "Flash สำเร็จ!"
                    ota_tasks[task_id]["logs"].append("✅ Flash สำเร็จ! ESP32 กำลัง reboot...")
                    print("✅ OTA Flash successful!")
                    save_log("✅ OTA Flash successful!")
                else:
                    error_msg = result.stderr.strip() or result.stdout.strip() or "Unknown error"
                    ota_tasks[task_id]["status"] = "error"
                    ota_tasks[task_id]["message"] = error_msg
                    ota_tasks[task_id]["logs"].append(f"❌ Flash failed: {error_msg}")
                    print(f"❌ OTA Flash failed: {error_msg}")
                    save_log(f"❌ OTA Flash failed: {error_msg}")

            except subprocess.TimeoutExpired:
                ota_tasks[task_id]["status"] = "error"
                ota_tasks[task_id]["message"] = "Timeout"
                ota_tasks[task_id]["logs"].append("❌ Timeout: Flash ใช้เวลานานเกินไป")
            except Exception as e:
                ota_tasks[task_id]["status"] = "error"
                ota_tasks[task_id]["message"] = str(e)
                ota_tasks[task_id]["logs"].append(f"❌ Error: {e}")

            try:
                os.remove(firmware_path)
            except:
                pass

        thread = threading.Thread(target=run_ota)
        thread.daemon = True
        thread.start()

        return jsonify({"status": "uploading", "task_id": task_id})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/ota/status/<task_id>')
def ota_status(task_id):
    """Check OTA flash status"""
    task = ota_tasks.get(task_id)
    if not task:
        return jsonify({"status": "error", "message": "Task not found"}), 404
    return jsonify(task)

# === WiFi Management ===
import re

@app.route('/wifi')
def wifi_page():
    return send_file('wifi.html')

@app.route('/api/wifi/status')
def wifi_status():
    """Get current WiFi connection info"""
    try:
        result = {}

        # Get current SSID
        ssid_out = subprocess.run(['iwgetid', '-r'], capture_output=True, text=True, timeout=5)
        result['ssid'] = ssid_out.stdout.strip() if ssid_out.returncode == 0 else ''
        result['connected'] = bool(result['ssid'])

        # Get IP — ใช้ ip addr show เฉพาะ interface เพื่อหลีกเลี่ยง Tailscale/VPN IP
        wlan0_ip = None
        ap0_ip = None
        try:
            out = subprocess.run(['ip', '-4', 'addr', 'show', 'wlan0'], capture_output=True, text=True, timeout=2)
            m = re.search(r'inet (\d+\.\d+\.\d+\.\d+)', out.stdout)
            if m:
                wlan0_ip = m.group(1)
        except Exception:
            pass
        try:
            out2 = subprocess.run(['ip', '-4', 'addr', 'show', 'ap0'], capture_output=True, text=True, timeout=2)
            m2 = re.search(r'inet (\d+\.\d+\.\d+\.\d+)', out2.stdout)
            if m2:
                ap0_ip = m2.group(1)
        except Exception:
            pass
        # Priority: wlan0 (home WiFi) > ap0 (hotspot fallback)
        result['ip'] = wlan0_ip or ap0_ip or ''

        # Get signal strength
        sig_out = subprocess.run(['iwconfig', 'wlan0'], capture_output=True, text=True, timeout=5)
        sig_match = re.search(r'Signal level=(-?\d+)', sig_out.stdout)
        result['signal'] = int(sig_match.group(1)) if sig_match else 0

        return jsonify(result)
    except Exception as e:
        return jsonify({"connected": False, "ssid": "", "ip": "", "signal": 0, "error": str(e)})

@app.route('/api/wifi/netstats')
def wifi_netstats():
    """Get network throughput bytes and ping latency"""
    stats = {"rx_bytes": 0, "tx_bytes": 0, "ping_ms": None}
    try:
        # Read interface bytes
        with open('/sys/class/net/wlan0/statistics/rx_bytes') as f:
            stats['rx_bytes'] = int(f.read().strip())
        with open('/sys/class/net/wlan0/statistics/tx_bytes') as f:
            stats['tx_bytes'] = int(f.read().strip())
    except Exception:
        pass
    try:
        # Ping 1.1.1.1
        p = subprocess.run(['ping', '-c', '1', '-W', '2', '1.1.1.1'],
                           capture_output=True, text=True, timeout=5)
        m = re.search(r'time=([0-9.]+)', p.stdout)
        if m:
            stats['ping_ms'] = round(float(m.group(1)), 1)
    except Exception:
        pass
    return jsonify(stats)

@app.route('/api/wifi/scan')
def wifi_scan():
    """Scan for available WiFi networks"""
    try:
        result = subprocess.run(
            ['sudo', 'iwlist', 'wlan0', 'scan'],
            capture_output=True, text=True, timeout=30
        )

        networks = []
        current_ssid = ''

        # Get current SSID
        ssid_out = subprocess.run(['iwgetid', '-r'], capture_output=True, text=True, timeout=5)
        if ssid_out.returncode == 0:
            current_ssid = ssid_out.stdout.strip()

        # Parse iwlist output
        cells = result.stdout.split('Cell ')
        for cell in cells[1:]:
            ssid_match = re.search(r'ESSID:"(.+?)"', cell)
            signal_match = re.search(r'Signal level=(-?\d+)', cell)
            enc_match = re.search(r'Encryption key:(on|off)', cell)

            if ssid_match and ssid_match.group(1):
                ssid = ssid_match.group(1)
                signal = int(signal_match.group(1)) if signal_match else -100
                encrypted = enc_match.group(1) == 'on' if enc_match else True

                # Skip duplicates (keep strongest signal)
                existing = next((n for n in networks if n['ssid'] == ssid), None)
                if existing:
                    if signal > existing['signal']:
                        existing['signal'] = signal
                    continue

                networks.append({
                    'ssid': ssid,
                    'signal': signal,
                    'encrypted': encrypted,
                    'active': ssid == current_ssid
                })

        # Sort: active first, then by signal strength
        networks.sort(key=lambda x: (-x['active'], -x['signal']))

        return jsonify({"networks": networks})
    except Exception as e:
        return jsonify({"networks": [], "error": str(e)}), 500

@app.route('/api/wifi/connect', methods=['POST'])
def wifi_connect():
    """Connect to a WiFi network using nmcli (NetworkManager)"""
    try:
        data = request.get_json()
        ssid = data.get('ssid', '').strip()
        password = data.get('password', '')

        if not ssid:
            return jsonify({"status": "error", "message": "SSID is required"}), 400

        print(f"📶 WiFi: Connecting to {ssid} via nmcli...")
        save_log(f"📶 WiFi config changed to: {ssid}")

        # Step 1: Delete existing WiFi connections (to allow switch)
        list_out = subprocess.run(
            ['nmcli', '-t', '-f', 'NAME,TYPE', 'connection', 'show'],
            capture_output=True, text=True, timeout=5
        )
        for line in list_out.stdout.strip().split('\n'):
            parts = line.split(':')
            if len(parts) >= 2 and parts[1] == '802-11-wireless':
                conn_name = parts[0]
                subprocess.run(
                    ['sudo', 'nmcli', 'connection', 'delete', conn_name],
                    capture_output=True, timeout=5
                )
                print(f"  🗑️ Deleted old connection: {conn_name}")

        # Step 2: Connect using nmcli device wifi connect
        import time as _time
        if password:
            connect_out = subprocess.run(
                ['sudo', 'nmcli', 'device', 'wifi', 'connect', ssid,
                 'password', password, 'ifname', 'wlan0'],
                capture_output=True, text=True, timeout=30
            )
        else:
            connect_out = subprocess.run(
                ['sudo', 'nmcli', 'device', 'wifi', 'connect', ssid,
                 'ifname', 'wlan0'],
                capture_output=True, text=True, timeout=30
            )

        print(f"  nmcli output: {connect_out.stdout.strip()}")
        if connect_out.stderr:
            print(f"  nmcli stderr: {connect_out.stderr.strip()}")

        # Step 3: Wait and verify
        _time.sleep(5)
        check = subprocess.run(['iwgetid', '-r'], capture_output=True, text=True, timeout=5)
        new_ssid = check.stdout.strip()

        if new_ssid == ssid:
            print(f"✅ WiFi connected to {ssid}")
            save_log(f"✅ WiFi connected to {ssid}")
            return jsonify({"status": "ok", "message": f"เชื่อมต่อ {ssid} สำเร็จ!"})
        else:
            print(f"⚠️ WiFi connection result: current={new_ssid}, target={ssid}")
            return jsonify({"status": "ok", "message": f"ส่งคำสั่งเชื่อมต่อ {ssid} แล้ว — อาจใช้เวลาสักครู่"})

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

# =============================================================================
# Web Terminal — WebSocket-to-Telnet Relay for ESP32 CLI
# =============================================================================
import socket as _socket

# Store active telnet connections per WebSocket session
_telnet_sessions = {}

@app.route('/terminal')
def terminal_page():
    return send_file('terminal.html')

@socketio.on('terminal_connect')
def handle_terminal_connect(data):
    """Open TCP connection to ESP32 Telnet and start reader thread"""
    sid = request.sid
    ip = data.get('ip', '192.168.10.2')
    port = int(data.get('port', 23))

    # Close existing session if any
    if sid in _telnet_sessions:
        try:
            _telnet_sessions[sid].close()
        except:
            pass
        del _telnet_sessions[sid]

    try:
        sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((ip, port))
        sock.settimeout(0.5)  # Non-blocking reads with short timeout
        _telnet_sessions[sid] = sock

        emit('terminal_connected', {'ip': ip, 'port': port})
        print(f"🖥️ Terminal connected to ESP32 at {ip}:{port} (sid={sid[:8]})")

        # Start background reader thread
        def reader():
            while sid in _telnet_sessions:
                try:
                    data = _telnet_sessions[sid].recv(4096)
                    if not data:
                        # Connection closed by ESP32
                        break
                    text = data.decode('utf-8', errors='replace')
                    socketio.emit('terminal_data', {'text': text}, to=sid)
                except _socket.timeout:
                    continue
                except Exception as e:
                    break

            # Cleanup
            if sid in _telnet_sessions:
                try:
                    _telnet_sessions[sid].close()
                except:
                    pass
                del _telnet_sessions[sid]
            socketio.emit('terminal_disconnected', {'reason': 'ESP32 connection closed'}, to=sid)
            print(f"🖥️ Terminal reader ended (sid={sid[:8]})")

        t = threading.Thread(target=reader, daemon=True)
        t.start()

    except _socket.timeout:
        emit('terminal_error', {'message': f'Connection timeout — ESP32 at {ip}:{port} not reachable'})
    except ConnectionRefusedError:
        emit('terminal_error', {'message': f'Connection refused — ESP32 at {ip}:{port} (Telnet not running?)'})
    except Exception as e:
        emit('terminal_error', {'message': f'Connection failed: {str(e)}'})

@socketio.on('terminal_input')
def handle_terminal_input(data):
    """Send user input to ESP32 via TCP"""
    sid = request.sid
    if sid not in _telnet_sessions:
        emit('terminal_error', {'message': 'Not connected'})
        return
    try:
        text = data.get('text', '')
        _telnet_sessions[sid].sendall(text.encode('utf-8'))
    except Exception as e:
        emit('terminal_error', {'message': f'Send failed: {str(e)}'})

@socketio.on('terminal_disconnect')
def handle_terminal_disconnect():
    """Close TCP connection to ESP32"""
    sid = request.sid
    if sid in _telnet_sessions:
        try:
            _telnet_sessions[sid].close()
        except:
            pass
        del _telnet_sessions[sid]
        emit('terminal_disconnected', {'reason': 'User disconnected'})
        print(f"🖥️ Terminal disconnected by user (sid={sid[:8]})")

@socketio.on('disconnect')
def handle_ws_disconnect():
    """Cleanup telnet session when WebSocket disconnects"""
    sid = request.sid
    if sid in _telnet_sessions:
        try:
            _telnet_sessions[sid].close()
        except:
            pass
        del _telnet_sessions[sid]
        print(f"🖥️ Terminal session cleaned up (sid={sid[:8]})")

if __name__ == '__main__':
    # Start MQTT Thread
    mqtt_thread = threading.Thread(target=start_mqtt)
    mqtt_thread.daemon = True
    mqtt_thread.start()
    
    # Start Heartbeat Monitor Thread
    monitor_thread = threading.Thread(target=monitor_heartbeat)
    monitor_thread.daemon = True
    monitor_thread.start()

    # Start WebSocket Broadcast Thread
    socketio.start_background_task(ws_broadcast)

    # Auto-start Camera
    try:
        import subprocess
        print("🎥 Starting Camera Service...")
        subprocess.Popen(["./start_cam.sh"], shell=True)
    except Exception as e:
        print(f"❌ Camera Start Error: {e}")
    
    print("🚀 Starting Web Server on port 80 (WebSocket enabled)...")
    socketio.run(app, host='0.0.0.0', port=80, debug=False, allow_unsafe_werkzeug=True)
