from flask import Flask, jsonify, send_file
import paho.mqtt.client as mqtt
import json
import threading
import psutil
import time

app = Flask(__name__)

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
    client.subscribe("aquaponics/logs") # Subscribe to logs

def on_message(client, userdata, msg):
    global last_data, last_esp_update, esp_online
    try:
        topic = msg.topic
        payload = msg.payload.decode()
        
        if topic == "aquaponics/logs":
            print(f"📝 Log: {payload}")
            save_log(payload)
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
    except Exception as e:
        print(f"❌ Error parsing MQTT: {e}")

# === Start MQTT in Background Thread ===
def start_mqtt():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        # "localhost" เพราะ Mosquitto อยู่เครื่องเดียวกับ app.py
        client.connect("localhost", 1883, 60)
        client.loop_forever()
    except Exception as e:
        print(f"❌ Could not connect to MQTT Broker: {e}")
        save_log(f"MQTT Error: {e}")

# === Web Server Routes ===
@app.route('/')
def index():
    return send_file('index.html')

@app.route('/api/sensors')
def get_sensors():
    return jsonify(last_data)

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

@app.route('/api/full_logs_file')
def get_full_logs_file():
    try:
        # Read the last 2000 lines to avoid crashing if file is huge
        lines = []
        with open(LOG_FILE, "r") as f:
            # Simple read for now
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

if __name__ == '__main__':
    # Start MQTT Thread
    mqtt_thread = threading.Thread(target=start_mqtt)
    mqtt_thread.daemon = True
    mqtt_thread.daemon = True
    mqtt_thread.start()
    
    # Start Heartbeat Monitor Thread
    monitor_thread = threading.Thread(target=monitor_heartbeat)
    monitor_thread.daemon = True
    monitor_thread.start()
    
    print("🚀 Starting Web Server on port 80...")
    app.run(host='0.0.0.0', port=80, debug=False)
