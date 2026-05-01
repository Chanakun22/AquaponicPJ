# 🐟 Smart Aquaponics System Project

**ระบบตรวจสอบคุณภาพน้ำอัจฉริยะสำหรับฟาร์มอควาโปนิกส์**

> Firmware v2.3.0 · ESP32-S3-DevKitC-1-N16R8 · Raspberry Pi Zero 2 W · PlatformIO + Arduino Framework

---

## 📋 สารบัญ

- [ภาพรวมระบบ](#-ภาพรวมระบบ)
- [ฟีเจอร์หลัก](#-ฟีเจอร์หลัก)
- [สถาปัตยกรรม](#-สถาปัตยกรรม)
- [ฮาร์ดแวร์](#-ฮาร์ดแวร์)
- [การติดตั้ง](#-การติดตั้ง)
- [การใช้งาน](#-การใช้งาน)
- [คู่มือทดสอบระบบ](#-คู่มือทดสอบระบบ)
- [Web Dashboard (Pi)](#-web-dashboard-pi)
- [คำสั่ง CLI](#-คำสั่ง-cli)
- [MQTT Topics](#-mqtt-topics)
- [โครงสร้างโปรเจค](#-โครงสร้างโปรเจค)
- [Tech Stack](#-tech-stack)

---

## 🌐 ภาพรวมระบบ

ระบบ Smart Aquaponics ประกอบด้วย **2 ส่วนหลัก**:

| ส่วน         | อุปกรณ์               | หน้าที่                                             |
| ------------ | --------------------- | --------------------------------------------------- |
| **Firmware** | ESP32-S3              | อ่านค่าเซ็นเซอร์, ควบคุมอุปกรณ์, ส่งข้อมูลผ่าน MQTT |
| **Server**   | Raspberry Pi Zero 2 W | Web Dashboard, MQTT Broker, กล้อง Live, OTA Update  |

Pi ทำหน้าที่เป็น **Access Point (AP)** ชื่อ `Aquaponics-LAN` ให้ ESP32 เชื่อมต่อโดยตรง พร้อมสามารถ Bridge อินเทอร์เน็ตจาก WiFi บ้านได้

---

## ✨ ฟีเจอร์หลัก

### 📊 เซ็นเซอร์ 5 ชนิด

| เซ็นเซอร์      | ค่าที่วัด                | ช่วงค่า         | ฟีเจอร์พิเศษ                            |
| -------------- | ------------------------ | --------------- | --------------------------------------- |
| **DS18B20**    | อุณหภูมิน้ำ              | 0-50°C          | Async Non-Blocking                      |
| **DHT22**      | อุณหภูมิอากาศ + ความชื้น | 0-50°C / 0-100% | —                                       |
| **TDS Sensor** | ค่า TDS (ppm)            | 0-2000 ppm      | 2-Point Calibration + Temp Compensation |
| **pH Sensor**  | ค่า pH                   | 0-14            | 3-Point Calibration (pH 4.01 / 6.86 / 9.18) |
| **BH1750**     | ความเข้มแสง              | 0-65535 lux     | I2C Digital                             |

### 📡 การเชื่อมต่อ

- **WiFi Manager** — Captive Portal ตั้งค่า WiFi ครั้งแรก ไม่ต้อง hardcode
- **NETPIE (Cloud MQTT)** — ส่งข้อมูลขึ้น Cloud ทุก 10 วินาที
- **Local MQTT (Pi)** — ส่งข้อมูลภายใน LAN ทุก 2 วินาที + ค้นหา Pi ผ่าน mDNS
- **OTA Update** — อัพเดต Firmware ผ่าน WiFi (ทั้ง PlatformIO CLI และ Web UI)
- **Telnet Console** — Debug ระยะไกล Port 23 (มี Password)

### 💡 ควบคุมแสง

- **NeoPixel RGB LED** (GPIO 48) บน ESP32-S3
- **Schedule System** — ตั้งเวลาเปิด/ปิดอัตโนมัติ รองรับข้ามวัน
- **Manual Override** — สั่งเปิด/ปิดผ่าน MQTT หรือ CLI
- **NTP Time Sync** — ซิงค์เวลาจาก Internet (GMT+7)

### 🛡️ ความเสถียร

- **Non-Blocking Design** — ไม่มี `delay()` ใน loop → ระบบไม่ค้าง
- **FreeRTOS Dual-Core** — Sensor (Core 1) / Network (Core 0) ทำงานคู่ขนาน
- **Watchdog Timer** — รีเซ็ตอัตโนมัติหากระบบค้างเกิน 60 วินาที
- **Auto Reconnect** — WiFi/MQTT หลุดแล้วเชื่อมใหม่อัตโนมัติ
- **Offline Mode** — เซ็นเซอร์ทำงานได้ 100% แม้ไม่มี WiFi
- **NVS Persistence** — เก็บ Calibration + สถิติระบบลง Flash

---

## 🏗️ สถาปัตยกรรม

```
┌─────────────────────────────────────────────────┐
│                 ESP32-S3 (Dual Core)             │
│                                                  │
│   ┌──────────────┐     ┌──────────────────────┐  │
│   │   Core 1     │     │      Core 0          │  │
│   │              │     │                      │  │
│   │ TaskSensors  │     │  TaskNetworking      │  │
│   │  - DS18B20   │     │   - WiFi Manager     │  │
│   │  - DHT22     │     │   - NETPIE MQTT      │  │
│   │  - TDS       │     │   - Local MQTT (Pi)  │  │
│   │  - pH        │     │   - OTA Update       │  │
│   │  - BH1750    │     │   - Telnet Server    │  │
│   │              │     │   - Serial Commands  │  │
│   │ TaskControl  │     │                      │  │
│   │  - Light Ctl │     │                      │  │
│   │  - System    │     │                      │  │
│   └──────┬───────┘     └──────────┬───────────┘  │
│          │  Shared Variables      │              │
│          └────────────────────────┘              │
└─────────────────────────────────────────────────┘
          │                          │
          ▼                          ▼
   ┌─────────────┐    ┌──────────────────────────┐
   │  NVS Flash  │    │   Raspberry Pi Zero 2 W  │
   │ Calibration │    │  ┌────────────────────┐  │
   │ Statistics  │    │  │ Flask Web Dashboard│  │
   └─────────────┘    │  │ MQTT Broker        │  │
                      │  │ Camera Server      │  │
                      │  │ SQLite Database    │  │
                      │  └────────────────────┘  │
                      │        │                 │
                      │   WiFi Bridge ──→ Internet
                      └──────────────────────────┘
                               │
                       ┌───────┴───────┐
                       │  NETPIE Cloud │
                       │  (MQTT)       │
                       └───────────────┘
```

---

## 🔌 ฮาร์ดแวร์

### บอร์ด

- **ESP32-S3-DevKitC-1-N16R8** (16MB QIO Flash, 8MB OPI PSRAM)
- **Raspberry Pi Zero 2 W** (Server + AP + Camera)

### การต่อสาย (Pin Configuration)

| อุปกรณ์           | GPIO    | หมายเหตุ                  |
| ----------------- | ------- | ------------------------- |
| TDS Sensor        | GPIO 5  | Analog Input              |
| DHT22             | GPIO 15 | Digital                   |
| DS18B20 (OneWire) | GPIO 13 | ต้องมี Pull-up 4.7kΩ      |
| pH Sensor         | GPIO 6  | Analog Input              |
| BH1750 SDA        | GPIO 8  | I2C                       |
| BH1750 SCL        | GPIO 9  | I2C                       |
| NeoPixel LED      | GPIO 48 | On-board RGB LED          |
| Factory Reset     | GPIO 0  | BOOT Button (กดค้าง 5 วิ) |

---

## 🚀 การติดตั้ง

### ข้อกำหนดเบื้องต้น

- [PlatformIO](https://platformio.org/) (แนะนำ VS Code Extension)
- ESP32-S3-DevKitC-1-N16R8
- Raspberry Pi Zero 2 W (สำหรับ Dashboard)

### ESP32 Firmware

> โปรเจกต์นี้ใช้ custom PlatformIO board manifest ที่ [boards/esp32-s3-devkitc-1-n16r8.json](boards/esp32-s3-devkitc-1-n16r8.json) เพื่อให้ค่าของ 16MB flash และ 8MB OPI PSRAM ตรงกับบอร์ดจริง
>
> OTA ใช้ `default_16MB.csv` ซึ่งแบ่ง app slot ไว้ประมาณ 6.25MB ต่อ slot เพียงพอกับขนาด firmware ปัจจุบันที่ build ได้ราว 0.94MB

1. **Clone โปรเจค**

   ```bash
   git clone <repo-url>
   cd test
   ```

2. **ตั้งค่า Secrets** — สร้างไฟล์ `secrets.ini` ใน root:

   ```ini
   [secrets]
   WIFI_AP_NAME = Aquaponics-Setup
   WIFI_AP_PASS = replace_with_unique_ap_password
   NETPIE_CLIENT_ID = your_client_id
   NETPIE_TOKEN = your_token
   NETPIE_SECRET = your_secret
   OTA_PASSWORD = your_ota_pass
   TELNET_PASSWORD = your_telnet_pass
   ```

3. **Build & Upload (USB)**

   ```bash
   pio run --target upload
   ```

4. **Build & Upload (OTA)**

   ```bash
   pio run -e ota_upload --target upload
   ```

5. **เปิด Serial Monitor**
   ```bash
   pio device monitor --baud 115200
   ```

### Raspberry Pi Server

1. **Copy ไฟล์ไปยัง Pi**

   ```bash
   scp pi_server/app.py pi_server/*.html admin@<pi-ip>:~/myserver/
   ```

2. **ติดตั้ง Dependencies**

   ```bash
   pip install flask flask-socketio requests psutil paho-mqtt
   ```

3. **รัน Server**

   ```bash
   python3 app.py
   ```

4. **ตั้งค่า AP (ถ้าต้องการให้ Pi เป็น Access Point)**
   ```bash
   sudo bash setup_ap.sh
   ```

---

## 📱 การใช้งาน

### การเริ่มใช้งานครั้งแรก (ESP32)

## 🧪 คู่มือทดสอบระบบ

สำหรับผู้ทดสอบที่ไม่ได้รู้โครงสร้างระบบมาก่อน ให้ใช้คู่มือ [TEST_OPERATOR_GUIDE.md](TEST_OPERATOR_GUIDE.md) เพื่อไล่ test แบบ step-by-step จาก Dashboard → Hardware Test → Settings → Safe Stop โดยไม่ต้องรู้รายละเอียดภายใน firmware ก่อน

1. จ่ายไฟเข้าบอร์ด
2. เชื่อมต่อ WiFi ชื่อ **`Aquaponics-Setup`** (หรือชื่อที่ตั้งไว้)
3. เปิด Browser ไปที่ `http://192.168.4.1`
4. เลือก WiFi แล้วใส่รหัสผ่าน
5. บอร์ดจะรีบูทและเชื่อมต่ออัตโนมัติ

### การ Calibrate เซ็นเซอร์

**pH Sensor** (ผ่าน Serial/Telnet):

```
cal686  → จุ่มในน้ำยาบัฟเฟอร์ pH 6.86 แล้วพิมพ์คำสั่ง
cal401  → จุ่มในน้ำยาบัฟเฟอร์ pH 4.01 แล้วพิมพ์คำสั่ง
cal918  → จุ่มในน้ำยาบัฟเฟอร์ pH 9.18 แล้วพิมพ์คำสั่ง
```

**TDS Sensor** (ผ่าน Web Dashboard):

- ใช้หน้า Settings → TDS Calibration ส่งค่า low/high voltage + ppm

### Factory Reset

- **กดปุ่ม BOOT ค้าง 5 วินาที** หรือพิมพ์ `reset` ใน CLI
- ล้างค่า WiFi, Calibration, และสถิติทั้งหมด

---

## 🖥️ Web Dashboard (Pi)

Dashboard รองรับทั้ง Desktop และ Mobile ผ่าน WebSocket (Real-time) หรือ HTTP Polling (Fallback)

| หน้า              | Route        | คำอธิบาย                                                      |
| ----------------- | ------------ | ------------------------------------------------------------- |
| **Dashboard**     | `/`          | แสดงค่าเซ็นเซอร์ทั้งหมด, สถานะ ESP32/Pi, System Logs          |
| **Live Camera**   | `/live`      | กล้อง Live Stream จาก Pi (รองรับหมุนภาพ)                      |
| **Graphs**        | `/graphs`    | กราฟข้อมูลเซ็นเซอร์ย้อนหลัง                                   |
| **Full Logs**     | `/full_logs` | บันทึก Log ทั้งหมดจากระบบ                                     |
| **Settings**      | `/settings`  | ตั้งค่า Threshold, Calibration, เปิด/ปิดเซ็นเซอร์, ตั้งเวลาไฟ |
| **OTA Update**    | `/ota`       | อัพเดต Firmware ESP32 ผ่าน Web UI (ลาก & วาง .bin)            |
| **WiFi Settings** | `/wifi`      | ตั้งค่า WiFi ของ Pi (สแกน, เชื่อมต่อเครือข่ายบ้าน)            |

---

## ⌨️ คำสั่ง CLI

ใช้ได้ทั้ง **Serial Monitor** และ **Telnet** (Port 23):

| คำสั่ง       | รายละเอียด                                |
| ------------ | ----------------------------------------- |
| `help`       | แสดงรายการคำสั่ง                          |
| `status`     | แสดงค่าเซ็นเซอร์ทั้งหมด                   |
| `test`       | รัน System Diagnostic ครบทุกส่วน          |
| `health`     | แสดงสุขภาพระบบ (Heap, Uptime, Reconnects) |
| `wifi`       | แสดงข้อมูล WiFi (SSID, IP, RSSI)          |
| `mqtt`       | แสดงสถานะ NETPIE MQTT                     |
| `ph`         | อ่านค่า pH + Voltage ปัจจุบัน             |
| `cal686`     | Calibrate pH จุด 6.86                     |
| `cal401`     | Calibrate pH จุด 4.01                     |
| `cal918`     | Calibrate pH จุด 9.18                     |
| `light on`   | เปิดไฟปลูกพืช (Manual)                    |
| `light off`  | ปิดไฟปลูกพืช (Manual)                     |
| `light auto` | กลับสู่โหมด Schedule                      |
| `version`    | แสดง Firmware Version                     |
| `reboot`     | รีสตาร์ทบอร์ด                             |
| `reset`      | Factory Reset (ล้างทุกอย่าง)              |
| `clear`      | ล้างหน้าจอ                                |

---

## 📝 MQTT Topics

### NETPIE (Cloud)

| Topic                       | ทิศทาง      | รายละเอียด                         |
| --------------------------- | ----------- | ---------------------------------- |
| `@shadow/data/update`       | ESP → Cloud | ส่งค่าเซ็นเซอร์                    |
| `@shadow/data/get/response` | Cloud → ESP | รับ Shadow data                    |
| `@shadow/data/updated`      | Cloud → ESP | Shadow เปลี่ยนแปลง                 |
| `@msg/#`                    | Cloud → ESP | คำสั่งควบคุม (lightOn/Off/Enabled) |

### Local MQTT (Raspberry Pi)

| Topic                       | ทิศทาง   | รายละเอียด                   |
| --------------------------- | -------- | ---------------------------- |
| `aquaponics/sensors`        | ESP → Pi | ค่าเซ็นเซอร์ + System Health |
| `aquaponics/logs`           | ESP → Pi | System Logs                  |
| `aquaponics/config/sensors` | Pi → ESP | เปิด/ปิดเซ็นเซอร์            |
| `aquaponics/config/tds_cal` | Pi → ESP | TDS Calibration Data         |
| `aquaponics/config/ph_cal`  | Pi → ESP | pH Calibration Data          |
| `aquaponics/status/sensors` | ESP → Pi | Feedback หลังเปลี่ยน Config  |

---

## 📁 โครงสร้างโปรเจค

```
├── include/                      # Header Files
│   ├── config.h                  # Pin Configuration & Constants
│   ├── logger.h                  # Logging System (LOG_ERROR/WARN/INFO/DEBUG)
│   ├── system.h                  # System Health & Sensor Management
│   ├── TdsSensor.h               # TDS Sensor Interface
│   ├── phSensor.h                # pH Sensor Interface
│   ├── dhtSensor.h               # DHT22 Interface
│   ├── tempSensor.h              # DS18B20 Interface
│   ├── lightSensor.h             # BH1750 Interface
│   ├── lightController.h         # Light Schedule Controller
│   ├── wifiConn.h                # WiFi Connection Manager
│   ├── netpie.h                  # NETPIE Cloud MQTT
│   ├── localMqtt.h               # Local MQTT (Raspberry Pi)
│   ├── ota.h                     # OTA Update
│   ├── telnetServer.h            # Telnet Debug Server
│   └── commandHandler.h          # CLI Command Handler
│
├── src/                          # Implementation Files
│   ├── main.cpp                  # Entry Point + FreeRTOS Tasks
│   ├── system.cpp                # System Management + NVS Persistence
│   ├── TdsSensor.cpp             # TDS with 2-Point Calibration
│   ├── phSensor.cpp              # pH with NVS Calibration
│   ├── dhtSensor.cpp             # DHT22 Sensor
│   ├── tempSensor.cpp            # DS18B20 Async Reading
│   ├── lightSensor.cpp           # BH1750 I2C Light Sensor
│   ├── lightController.cpp       # NeoPixel RGB + Schedule Logic
│   ├── wifiConn.cpp              # WiFi Manager (Non-Blocking)
│   ├── netpie.cpp                # NETPIE MQTT Client
│   ├── localMqtt.cpp             # Local MQTT + mDNS Discovery
│   ├── ota.cpp                   # OTA Update Handler
│   ├── telnetServer.cpp          # Telnet with Authentication
│   └── commandHandler.cpp        # CLI Command Parser
│
├── pi_server/                    # Raspberry Pi Server
│   ├── app.py                    # Flask Backend (API + WebSocket)
│   ├── cam_server.py             # Camera Live Stream Server
│   ├── espota.py                 # ESP32 OTA Flash Script
│   ├── index.html                # Main Dashboard
│   ├── live.html                 # Live Camera Page
│   ├── graphs.html               # Sensor Graphs Page
│   ├── full_logs.html            # Full Logs Page
│   ├── settings.html             # Settings & Calibration Page
│   ├── ota.html                  # OTA Firmware Upload Page
│   ├── wifi.html                 # Pi WiFi Settings Page
│   ├── settings.json             # Default Settings
│   ├── setup.sh                  # Pi Install Script
│   ├── setup_ap.sh               # AP + Bridge Setup Script
│   ├── hostapd.conf              # AP Configuration
│   ├── dnsmasq_ap.conf           # DHCP Configuration
│   └── pwa/                      # PWA Manifest & Icons
│
├── lib/WiFiManager/              # WiFiManager Library (Custom Fork)
├── platformio.ini                # PlatformIO Configuration
├── secrets.ini                   # Credentials (ไม่อยู่ใน Git)
├── CHANGELOG.md                  # บันทึกการเปลี่ยนแปลง
└── README.md                     # ← คุณอยู่ที่นี่
```

---

## 📦 Tech Stack

### ESP32 Firmware

| Component        | Technology                  | Version                       |
| ---------------- | --------------------------- | ----------------------------- |
| **MCU**          | ESP32-S3-DevKitC-1-N16R8    | 240MHz, 320KB RAM, 16MB Flash, 8MB OPI PSRAM |
| **Platform**     | Espressif 32                | 6.4.0                         |
| **Framework**    | Arduino Core for ESP32      | 2.0.11                        |
| **RTOS**         | FreeRTOS                    | (built-in)                    |
| **MQTT**         | PubSubClient                | 2.8.0                         |
| **JSON**         | ArduinoJson                 | 6.21.5                        |
| **WiFi Config**  | WiFiManager                 | 2.0.17                        |
| **LED**          | Adafruit NeoPixel           | 1.12.0                        |
| **Temp (Water)** | DallasTemperature + OneWire | 3.11.0 / 2.3.8                |
| **Temp (Air)**   | DHT sensor library          | 1.4.6                         |
| **Light**        | BH1750                      | 1.3.0                         |

### Raspberry Pi Server

| Component         | Technology                  |
| ----------------- | --------------------------- |
| **OS**            | Raspberry Pi OS (Lite)      |
| **Web Framework** | Flask + Flask-SocketIO      |
| **MQTT Broker**   | Mosquitto                   |
| **Database**      | SQLite                      |
| **Camera**        | picamera2 + Flask Streaming |
| **AP Mode**       | hostapd + dnsmasq           |

---

## 📊 Resource Usage

```
RAM:   [==        ]  16.0% (52,592 / 327,680 bytes)
Flash: [=         ]  14.9% (975,765 / 6,553,600 bytes)
```

---

## 📄 License

This project is for educational purposes.
