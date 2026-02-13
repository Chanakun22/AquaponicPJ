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
        "light": {
            "enabled": False,
            "on_day": 1, "on_time": "06:00",
            "off_day": 5, "off_time": "18:00"
        },
        "notifications": {
            "line_token": "",
            "alert_cooldown_min": 15,
            "enabled": False
        },
        "display": {
            "graph_days": 3,
            "refresh_sec": 5,
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
            "framerate": 15
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
            conn.close()
            print("✅ Database initialized")
        except Exception as e:
            print(f"❌ Database Error: {e}")

# Initialize DB on start
init_db()

def save_settings_to_db(settings):
    """Save settings snapshot to database"""
    with db_lock:
        try:
            conn = sqlite3.connect(DB_FILE)
            cursor = conn.cursor()
            cursor.execute('INSERT INTO settings_history (settings_json) VALUES (?)', (json.dumps(settings),))
            conn.commit()
            conn.close()
            # print("💾 Settings saved to DB history")
        except Exception as e:
            print(f"❌ Failed to save settings to DB: {e}")

def save_settings(settings):
    """Save settings to JSON file and Database"""
    try:
        # 1. Save to File (Actual Config)
        with open(SETTINGS_FILE, "w") as f:
            json.dump(settings, f, indent=2)
            
        # 2. Save to DB (History/Backup)
        save_settings_to_db(settings)
        
        return True
    except Exception as e:
        print(f"Error saving settings: {e}")
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
            conn.close()
            last_db_save = now
            # print("💾 Data saved to DB") 
        except Exception as e:
            print(f"DB Insert Error: {e}")

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
        
        # Validate
        valid_resolutions = [(640, 480), (1280, 720), (1920, 1080)]
        if (width, height) not in valid_resolutions:
            return jsonify({'status': 'error', 'message': 'Invalid resolution'}), 400
        if framerate < 1 or framerate > 30:
            return jsonify({'status': 'error', 'message': 'FPS must be 1-30'}), 400
        
        # Save to settings
        app_settings['camera'] = {
            'width': width,
            'height': height,
            'framerate': framerate
        }
        save_settings(app_settings)
        save_log(f"📷 Camera settings updated: {width}x{height} @ {framerate}fps")
        
        # Restart camera server
        import subprocess
        try:
            subprocess.run(['sudo', 'pkill', '-f', 'cam_server.py'], timeout=5)
            import time as _time
            _time.sleep(1)
            cam_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'start_cam.sh')
            subprocess.Popen(['bash', cam_script])
            print(f"📷 Camera restarted: {width}x{height} @ {framerate}fps")
        except Exception as ex:
            print(f"⚠️ Camera restart error: {ex}")
        
        return jsonify({'status': 'ok', 'message': f'Camera restarting with {width}x{height} @ {framerate}fps'})
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
