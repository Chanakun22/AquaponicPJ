# 🐟 Smart Aquaponics Sensor System

**ระบบตรวจสอบคุณภาพน้ำอัจฉริยะสำหรับฟาร์มอควาโปนิกส์**

> Firmware v2.3.0 | ESP32-S3-DevKitC-1 | PlatformIO + Arduino Framework

---

## 📋 สารบัญ

- [ฟีเจอร์หลัก](#-ฟีเจอร์หลัก)
- [สถาปัตยกรรมระบบ](#-สถาปัตยกรรมระบบ)
- [ฮาร์ดแวร์](#-ฮาร์ดแวร์)
- [การติดตั้ง](#-การติดตั้ง)
- [การใช้งาน](#-การใช้งาน)
- [คำสั่ง CLI](#-คำสั่ง-cli)
- [โครงสร้างโปรเจค](#-โครงสร้างโปรเจค)
- [Tech Stack](#-tech-stack)

---

## ✨ ฟีเจอร์หลัก

### 📊 เซ็นเซอร์ 5 ชนิด

| เซ็นเซอร์      | ค่าที่วัด                | ช่วงค่า         | ฟีเจอร์พิเศษ                                   |
| -------------- | ------------------------ | --------------- | ---------------------------------------------- |
| **DS18B20**    | อุณหภูมิน้ำ              | 0-50°C          | Async Non-Blocking                             |
| **DHT22**      | อุณหภูมิอากาศ + ความชื้น | 0-50°C / 0-100% | -                                              |
| **TDS Sensor** | ค่า TDS (ppm)            | 0-2000 ppm      | 2-Point Calibration + Temperature Compensation |
| **pH Sensor**  | ค่า pH                   | 0-14            | NVS Calibration (pH 4.0 & 7.0)                 |
| **BH1750**     | ความเข้มแสง              | 0-65535 lux     | I2C Digital                                    |

### 📡 การเชื่อมต่อ

- **WiFi Manager** — Captive Portal ตั้งค่า WiFi ครั้งแรก ไม่ต้อง hardcode
- **NETPIE (Cloud MQTT)** — ส่งข้อมูลขึ้น Cloud ทุก 10 วินาที
- **Local MQTT (Raspberry Pi)** — ส่งข้อมูลภายใน LAN ทุก 2 วินาที + ค้นหา Pi ผ่าน mDNS
- **OTA Update** — อัพเดต Firmware ผ่าน WiFi
- **Telnet Console** — Debug ระยะไกล Port 23 (มี Password)

### 💡 ควบคุมแสง

- **NeoPixel RGB LED** (GPIO 48) บน ESP32-S3
- **Schedule System** — ตั้งเวลาเปิด/ปิดอัตโนมัติ รองรับข้ามวัน
- **Manual Override** — สั่งเปิด/ปิดผ่าน MQTT หรือ CLI
- **NTP Time Sync** — ซิงค์เวลาจาก Internet (GMT+7)

### 🛡️ ความเสถียร

- **Non-Blocking Design** — ไม่มี `delay()` ใน loop → ระบบไม่ค้าง
- **FreeRTOS Multi-Core** — Sensor (Core 1) / Network (Core 0) ทำงานคู่ขนาน
- **Watchdog Timer** — รีเซ็ตอัตโนมัติหากระบบค้างเกิน 60 วินาที
- **Auto Reconnect** — WiFi/MQTT หลุดแล้วเชื่อมใหม่อัตโนมัติ
- **Offline Mode** — เซ็นเซอร์ทำงานได้ 100% แม้ไม่มี WiFi
- **NVS Persistence** — เก็บ Calibration + สถิติระบบลง Flash

---

## 🏗️ สถาปัตยกรรมระบบ

```
┌─────────────────────────────────────────────────┐
│                  ESP32-S3 (Dual Core)            │
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
   ┌─────────────┐         ┌──────────────────┐
   │  NVS Flash  │         │  Raspberry Pi    │
   │ Calibration │         │  Dashboard +     │
   │ Statistics  │         │  MQTT Broker     │
   └─────────────┘         └──────────────────┘
                                     │
                             ┌───────┴───────┐
                             │  NETPIE Cloud │
                             │  (MQTT)       │
                             └───────────────┘
```

---

## 🔌 ฮาร์ดแวร์

### บอร์ด

- **ESP32-S3-DevKitC-1** (8MB Flash, No PSRAM)

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
- ESP32-S3-DevKitC-1

### ขั้นตอน

1. **Clone โปรเจค**

   ```bash
   git clone <repo-url>
   cd test
   ```

2. **ตั้งค่า Secrets** — สร้างไฟล์ `secrets.ini` ใน root:

   ```ini
   [secrets]
   WIFI_AP_NAME = Aquaponics-Setup
   WIFI_AP_PASS = admin1234
   NETPIE_CLIENT_ID = your_client_id
   NETPIE_TOKEN = your_token
   NETPIE_SECRET = your_secret
   OTA_PASSWORD = your_ota_pass
   TELNET_PASSWORD = your_telnet_pass
   ```

3. **Build & Upload**

   ```bash
   pio run --target upload
   ```

4. **เปิด Serial Monitor**
   ```bash
   pio device monitor --baud 115200
   ```

---

## � การใช้งาน

### การเริ่มใช้งานครั้งแรก

1. จ่ายไฟเข้าบอร์ด
2. เชื่อมต่อ WiFi ชื่อ **`Aquaponics-Setup`** (หรือชื่อที่ตั้งไว้)
3. เปิด Browser ไปที่ `http://192.168.4.1`
4. เลือก WiFi บ้านและใส่รหัสผ่าน
5. บอร์ดจะรีบูทและเชื่อมต่ออัตโนมัติ

### การ Calibrate เซ็นเซอร์

**pH Sensor** (ผ่าน Serial/Telnet):

```
cal7    → จุ่มในน้ำยาบัฟเฟอร์ pH 7.0 แล้วพิมพ์คำสั่ง
cal4    → จุ่มในน้ำยาบัฟเฟอร์ pH 4.0 แล้วพิมพ์คำสั่ง
```

**TDS Sensor** (ผ่าน MQTT จาก Pi Dashboard):

- ใช้หน้า Settings บน Web Dashboard ส่งค่า low/high voltage + ppm

### Factory Reset

- **กดปุ่ม BOOT ค้าง 5 วินาที** หรือพิมพ์ `reset` ใน CLI
- ล้างค่า WiFi, Calibration, และสถิติทั้งหมด

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
| `cal7`       | Calibrate pH จุด 7.0                      |
| `cal4`       | Calibrate pH จุด 4.0                      |
| `light on`   | เปิดไฟปลูกพืช (Manual)                    |
| `light off`  | ปิดไฟปลูกพืช (Manual)                     |
| `light auto` | กลับสู่โหมด Schedule                      |
| `version`    | แสดง Firmware Version                     |
| `reboot`     | รีสตาร์ทบอร์ด                             |
| `reset`      | Factory Reset (ล้างทุกอย่าง)              |
| `clear`      | ล้างหน้าจอ                                |

---

## 📁 โครงสร้างโปรเจค

```
├── include/                  # Header Files
│   ├── config.h              # Central Configuration (Pins, Timing, Secrets)
│   ├── logger.h              # Logging System (LOG_ERROR/WARN/INFO/DEBUG)
│   ├── system.h              # System Health, Version, Sensor Management
│   ├── TdsSensor.h           # TDS Sensor Interface
│   ├── phSensor.h            # pH Sensor Interface
│   ├── dhtSensor.h           # DHT22 Interface
│   ├── tempSensor.h          # DS18B20 Interface
│   ├── lightSensor.h         # BH1750 Interface
│   ├── lightController.h     # Light Schedule Controller
│   ├── wifiConn.h            # WiFi Connection Manager
│   ├── netpie.h              # NETPIE Cloud MQTT
│   ├── localMqtt.h           # Local MQTT (Raspberry Pi)
│   ├── ota.h                 # OTA Update
│   ├── telnetServer.h        # Telnet Debug Server
│   └── commandHandler.h      # CLI Command Handler
│
├── src/                      # Implementation Files
│   ├── main.cpp              # Entry Point + FreeRTOS Tasks
│   ├── system.cpp            # System Management + NVS Persistence
│   ├── TdsSensor.cpp         # TDS with 2-Point Calibration
│   ├── phSensor.cpp          # pH with NVS Calibration
│   ├── dhtSensor.cpp         # DHT22 Sensor
│   ├── tempSensor.cpp        # DS18B20 Async Reading
│   ├── lightSensor.cpp       # BH1750 I2C Light Sensor
│   ├── lightController.cpp   # NeoPixel RGB + Schedule Logic
│   ├── wifiConn.cpp          # WiFi Manager (Non-Blocking)
│   ├── netpie.cpp            # NETPIE MQTT Client
│   ├── localMqtt.cpp         # Local MQTT + mDNS Discovery
│   ├── ota.cpp               # OTA Update Handler
│   ├── telnetServer.cpp      # Telnet with Authentication
│   └── commandHandler.cpp    # CLI Command Parser
│
├── lib/WiFiManager/          # WiFiManager Library (Custom Fork)
├── pi_server/                # Raspberry Pi Web Dashboard
│   ├── settings.html         # Threshold & Calibration Settings
│   ├── graphs.html           # Sensor Data Graphs
│   ├── live.html             # Live Camera Feed
│   └── setup.sh              # Pi Auto-Install Script
│
├── platformio.ini            # PlatformIO Configuration
├── secrets.ini               # Credentials (ไม่อยู่ใน Git)
└── README.md                 # ← คุณอยู่ที่นี่
```

---

## 📦 Tech Stack

| Component        | Technology                  | Version                      |
| ---------------- | --------------------------- | ---------------------------- |
| **MCU**          | ESP32-S3-DevKitC-1-N8       | 240MHz, 320KB RAM, 8MB Flash |
| **Platform**     | Espressif 32                | 6.4.0                        |
| **Framework**    | Arduino Core for ESP32      | 2.0.11                       |
| **RTOS**         | FreeRTOS                    | (built-in)                   |
| **MQTT**         | PubSubClient                | 2.8.0                        |
| **JSON**         | ArduinoJson                 | 6.21.5                       |
| **WiFi Config**  | WiFiManager                 | 2.0.17                       |
| **LED**          | Adafruit NeoPixel           | 1.15.2                       |
| **Temp (Water)** | DallasTemperature + OneWire | 3.11.0 / 2.3.8               |
| **Temp (Air)**   | DHT sensor library          | 1.4.6                        |
| **Light**        | BH1750                      | 1.3.0                        |

---

## 📊 Resource Usage

```
RAM:   [==        ]  16.0% (52,592 / 327,680 bytes)
Flash: [=         ]  14.9% (975,765 / 6,553,600 bytes)
```

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
| `aquaponics/status/sensors` | ESP → Pi | Feedback หลังเปลี่ยน Config  |

---

## 📄 License

This project is for educational purposes.
