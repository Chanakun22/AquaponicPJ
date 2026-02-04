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

# === MQTT Functions ===
def on_connect(client, userdata, flags, rc):
    print(f"✅ MQTT Connected with result code {rc}")
    client.subscribe("aquaponics/sensors")

def on_message(client, userdata, msg):
    global last_data
    try:
        payload = msg.payload.decode()
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

# === Web Server Routes ===
@app.route('/')
def index():
    return send_file('index.html')

@app.route('/api/sensors')
def get_sensors():
    return jsonify(last_data)

@app.route('/api/health')
def get_health():
    # Update Real-time Pi Stats
    last_data["pi_cpu_percent"] = psutil.cpu_percent()
    mem = psutil.virtual_memory()
    last_data["pi_ram_total"] = mem.total
    last_data["pi_ram_used"] = mem.used
    last_data["pi_ram_percent"] = mem.percent
    last_data["pi_temp"] = get_pi_temp()
    
    return jsonify(last_data) 

@app.route('/api/info')
def get_info():
    return jsonify({"firmware": "Pi-Server-v2 (Monitoring)", "status": "online"})

if __name__ == '__main__':
    # Start MQTT Thread
    mqtt_thread = threading.Thread(target=start_mqtt)
    mqtt_thread.daemon = True
    mqtt_thread.start()
    
    print("🚀 Starting Web Server on port 80...")
    app.run(host='0.0.0.0', port=80, debug=False)
