# 🌿 Aquaponics Sensor System v2.3 (Production)

ระบบตรวจวัดและควบคุมคุณภาพน้ำสำหรับ Aquaponics พร้อมเชื่อมต่อ WiFi และ NETPIE IoT Platform

**Production-Ready Features:**
- ✅ Structured Logging System (Conditional Debug)
- ✅ OTA Update Support
- ✅ System Health Monitoring
- ✅ Factory Reset Capability
- ✅ Error Recovery & Resilience
- ✅ Memory Management
- ✅ Version Tracking

---

## � สารบัญ

1. [ภาพรวมระบบ](#ภาพรวมระบบ)
2. [Hardware Configuration](#hardware-configuration)
3. [Sensors](#sensors)
4. [Light Controller](#light-controller)
5. [NETPIE Integration](#netpie-integration)
6. [pH Calibration](#ph-calibration)
7. [Serial Commands](#serial-commands)
8. [Project Structure](#project-structure)
9. [Configuration](#configuration)
10. [Installation](#installation)

---

## ภาพรวมระบบ

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        ESP32-S3                              │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐     │
│  │   TDS    │  │   pH     │  │  DS18B20 │  │  DHT22   │     │
│  │  Sensor  │  │  Sensor  │  │  (Water) │  │ (Air T/H)│     │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘     │
│       │GPIO 5       │GPIO 6       │GPIO 4       │GPIO 15    │
│       └─────────────┴─────────────┴─────────────┘           │
│                           │                                  │
│  ┌──────────┐  ┌──────────────┐  ┌─────────────────┐        │
│  │  BH1750  │  │ NeoPixel LED │  │  WiFi Module    │        │
│  │  (Light) │  │  (Status)    │  │                 │        │
│  └────┬─────┘  └──────┬───────┘  └────────┬────────┘        │
│       │I2C           │GPIO 48             │                  │
│       │SDA=8, SCL=9  │                    │                  │
└───────┴──────────────┴────────────────────┴──────────────────┘
                                            │
                                            ▼
                              ┌─────────────────────────┐
                              │     NETPIE Cloud        │
                              │  ┌─────────────────┐    │
                              │  │  Shadow (State) │    │
                              │  │  - Sensor Data  │    │
                              │  │  - Light Ctrl   │    │
                              │  │  - pH Calib     │    │
                              │  └─────────────────┘    │
                              └─────────────────────────┘
```

### Features

- ✅ **Multi-Sensor Support** - TDS, pH, Temperature, Humidity, Light
- ✅ **Real-time Monitoring** - ส่งข้อมูลไป NETPIE ทุก 60 วินาที
- ✅ **Multi-Day Light Schedule** - ตั้งเวลาเปิด/ปิดไฟข้ามวันได้
- ✅ **pH Calibration** - Calibrate ผ่าน Serial หรือ NETPIE
- ✅ **Non-blocking Design** - ทุก module ทำงานแบบ non-blocking
- ✅ **WiFiManager** - ตั้งค่า WiFi ผ่าน Captive Portal
- ✅ **OTA Updates** - อัพเดท firmware ผ่าน WiFi (ArduinoOTA)
- ✅ **System Health** - ตรวจสอบ memory, uptime, reconnects
- ✅ **Production Logging** - Structured logs with log levels
- ✅ **Factory Reset** - Reset ทุกการตั้งค่าด้วยปุ่ม BOOT
- ✅ **Error Recovery** - Auto-reconnect WiFi/MQTT

---

## Hardware Configuration

### ESP32-S3 DevKitC-1 Pin Mapping

| GPIO | Function | Component  | Description            |
| ---- | -------- | ---------- | ---------------------- |
| 4    | OneWire  | DS18B20    | อุณหภูมิน้ำ            |
| 5    | Analog   | TDS Sensor | ค่า TDS (ppm)          |
| 6    | Analog   | pH Sensor  | ค่า pH (0-14)          |
| 8    | I2C SDA  | BH1750     | Light Sensor           |
| 9    | I2C SCL  | BH1750     | Light Sensor           |
| 15   | Digital  | DHT22      | อุณหภูมิ/ความชื้นอากาศ |
| 48   | NeoPixel | RGB LED    | แสดงสถานะไฟ            |

### Wiring Diagram

```
ESP32-S3              Sensors
─────────             ───────

GPIO 4  ───────────── DS18B20 DATA (with 4.7k pull-up)
GPIO 5  ───────────── TDS Sensor Signal
GPIO 6  ───────────── pH Sensor Signal (Po)
GPIO 8  ───────────── BH1750 SDA
GPIO 9  ───────────── BH1750 SCL
GPIO 15 ───────────── DHT22 DATA (with 10k pull-up)
GPIO 48 ───────────── Onboard NeoPixel LED
3.3V    ───────────── All Sensors VCC
GND     ───────────── All Sensors GND
```

---

## Sensors

### 1. TDS Sensor (TdsSensor.cpp)

**วัตถุประสงค์:** วัดค่า Total Dissolved Solids ในน้ำ

**หลักการทำงาน:**

1. อ่านค่า ADC จาก GPIO 5 (12-bit = 0-4095)
2. แปลงเป็น voltage (0-3.3V)
3. ใช้ Median Filter กรองค่า outliers
4. Temperature Compensation จากอุณหภูมิน้ำ
5. คำนวณเป็นค่า TDS (ppm)

**สูตรคำนวณ:**

```cpp
compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
compensatedVoltage = voltage / compensationCoefficient;
tds = (133.42 * cv³ - 255.86 * cv² + 857.39 * cv) * 0.5;
```

### 2. pH Sensor (phSensor.cpp)

**วัตถุประสงค์:** วัดค่าความเป็นกรด-ด่างของน้ำ

**หลักการทำงาน:**

1. อ่านค่า ADC จาก GPIO 6
2. ใช้ Median Filter (30 samples)
3. แปลงเป็น voltage
4. คำนวณ pH จาก Nernst Equation

**Nernst Equation:**

```
pH = 7.0 + (Voltage - Voltage@pH7) / Slope
Slope = -59.16 mV/pH @ 25°C (theoretical)
```

**Calibration:**

- ต้อง calibrate 2 จุด: pH 4.0 และ pH 7.0
- ระบบจะคำนวณ slope จริงจากทั้งสองจุด

### 3. DS18B20 Temperature Sensor (tempSensor.cpp)

**วัตถุประสงค์:** วัดอุณหภูมิน้ำ

**หลักการทำงาน:**

- ใช้ Dallas OneWire Protocol
- ความละเอียด 12-bit (0.0625°C)
- อ่านค่าแบบ non-blocking

### 4. DHT22 Sensor (dhtSensor.cpp)

**วัตถุประสงค์:** วัดอุณหภูมิและความชื้นอากาศ

**Specifications:**

- Temperature: -40 to 80°C (±0.5°C)
- Humidity: 0-100% (±2-5%)
- อ่านค่าทุก 2 วินาที

### 5. BH1750 Light Sensor (lightSensor.cpp)

**วัตถุประสงค์:** วัดความเข้มแสง

**Specifications:**

- ช่วง: 1-65535 lux
- I2C Address: 0x23
- Continuous High Resolution Mode

---

## Light Controller

### หลักการทำงาน

Light Controller ควบคุมการเปิด/ปิดไฟตามตารางเวลา

### Mode การทำงาน

| lightEnabled | Mode     | Description            |
| ------------ | -------- | ---------------------- |
| 0            | Manual   | ควบคุมด้วย lightRelay  |
| 1            | Schedule | ควบคุมอัตโนมัติตามเวลา |

### Multi-Day Schedule

ระบบรองรับ schedule ข้ามหลายวัน:

**ตัวอย่าง:** เปิดวันจันทร์ 16:00 → ปิดวันอาทิตย์ 16:00

```
lightOnDay: 1      (Monday)
lightOnTime: "16:00"
lightOffDay: 0     (Sunday)
lightOffTime: "16:00"
```

**Logic:**

```cpp
// แปลงเป็น "week minutes" (0-10079)
int toWeekMinutes(int day, int hour, int minute) {
    return day * 24 * 60 + hour * 60 + minute;
}

// Monday 16:00 = 1 * 1440 + 960 = 2400
// Sunday 16:00 = 0 * 1440 + 960 = 960

// ถ้า ON > OFF = ข้ามสัปดาห์
if (on >= current || current < off) → ON
```

### NeoPixel LED Status

| State | Color    | Description |
| ----- | -------- | ----------- |
| ON    | 🟢 Green | ไฟเปิด      |
| OFF   | ⚫ Off   | ไฟปิด       |

---

## NETPIE Integration

### Connection

- **Broker:** mqtt.netpie.io
- **Port:** 1883
- **Protocol:** MQTT

### Topics

| Topic                     | Direction | Description        |
| ------------------------- | --------- | ------------------ |
| @shadow/data/update       | Publish   | ส่งข้อมูลเซ็นเซอร์ |
| @shadow/data/get          | Publish   | ขอ Shadow ปัจจุบัน |
| @shadow/data/get/response | Subscribe | รับ Shadow         |
| @shadow/data/updated      | Subscribe | Real-time updates  |

### Shadow Data Structure

```json
{
  "data": {
    "waterTemp": 27.5,
    "airTemp": 30.2,
    "humidity": 65.3,
    "tds": 450.0,
    "light": 1500.0,
    "ph": 6.85,
    "lightRelay": 1,
    "lightEnabled": 1,
    "lightOnDay": 1,
    "lightOnTime": "06:00",
    "lightOffDay": 0,
    "lightOffTime": "18:00"
  }
}
```

### Data Flow

```
ESP32 Boot
    │
    ├── Subscribe to @shadow/data/get/response
    ├── Subscribe to @shadow/data/updated
    │
    ├── Request Shadow (@shadow/data/get)
    │       │
    │       └── Receive current settings
    │
    └── Every 60 seconds:
            │
            └── Publish sensor data (@shadow/data/update)

NETPIE Widget Change
    │
    └── @shadow/data/updated
            │
            └── ESP32 receives & applies immediately
```

---

## pH Calibration

### ทำไมต้อง Calibrate?

pH probe แต่ละตัวมี offset และ slope ที่แตกต่างกัน
ต้อง calibrate เพื่อให้ค่าที่อ่านได้แม่นยำ

### Calibration Steps

#### ขั้นตอนที่ 1: Calibrate pH 7.0 (Neutral)

1. เตรียม **pH 7.0 Buffer Solution**
2. ล้าง probe ด้วยน้ำกลั่น
3. จุ่ม probe ใน buffer
4. รอ **30-60 วินาที** ให้ค่าคงที่
5. ส่งคำสั่ง calibrate:
   - Serial: `cal7`
   - NETPIE: `{"data":{"phCal7":1}}`

#### ขั้นตอนที่ 2: Calibrate pH 4.0 (Acidic)

1. เตรียม **pH 4.0 Buffer Solution**
2. ล้าง probe ด้วยน้ำกลั่น
3. จุ่ม probe ใน buffer
4. รอ **30-60 วินาที** ให้ค่าคงที่
5. ส่งคำสั่ง calibrate:
   - Serial: `cal4`
   - NETPIE: `{"data":{"phCal4":1}}`

### Calibration Math

```
After calibrating both points:

slope = (voltage@pH4 - voltage@pH7) / (4.0 - 7.0)

For any reading:
pH = 7.0 + (currentVoltage - voltage@pH7) / slope
```

---

## Serial Commands

เปิด Serial Monitor (115200 baud) และพิมพ์คำสั่ง:

| Command  | Description                     |
| -------- | ------------------------------- |
| `cal7`   | Calibrate pH 7.0                |
| `cal4`   | Calibrate pH 4.0                |
| `ph`     | แสดงค่า pH และ Voltage ปัจจุบัน |
| `health` | แสดง system health status      |
| `reset`  | Factory reset (ลบทุกการตั้งค่า) |
| `help`   | แสดงรายการ commands             |

---

## Project Structure

```
test/
├── include/                    # Header files
│   ├── config.h               # Pin & Configuration
│   ├── logger.h               # Production Logging System
│   ├── system.h               # System Management
│   ├── ota.h                  # OTA Update Support
│   ├── TdsSensor.h            # TDS Sensor interface
│   ├── dhtSensor.h            # DHT22 interface
│   ├── tempSensor.h           # DS18B20 interface
│   ├── lightSensor.h          # BH1750 interface
│   ├── phSensor.h             # pH Sensor interface
│   ├── lightController.h      # Light Control interface
│   ├── wifiConn.h             # WiFi Manager interface
│   └── netpie.h               # NETPIE MQTT interface
│
├── src/                        # Source files
│   ├── main.cpp               # Main program loop
│   ├── system.cpp             # System Management
│   ├── ota.cpp                # OTA Update
│   ├── TdsSensor.cpp          # TDS implementation
│   ├── dhtSensor.cpp          # DHT22 implementation
│   ├── tempSensor.cpp         # DS18B20 implementation
│   ├── lightSensor.cpp        # BH1750 implementation
│   ├── phSensor.cpp           # pH implementation
│   ├── lightController.cpp    # Light Control implementation
│   ├── wifiConn.cpp           # WiFi implementation
│   └── netpie.cpp             # NETPIE implementation
│
├── platformio.ini              # PlatformIO configuration
└── README.md                   # This file
```

---

## Configuration

### config.h Settings

```cpp
// === NETPIE Credentials ===
#define NETPIE_CLIENT_ID    "your-client-id"
#define NETPIE_TOKEN        "your-token"
#define NETPIE_SECRET       "your-secret"

// === WiFi ===
#define WIFI_AP_NAME        "AquaponicsAP"
#define WIFI_AP_PASSWORD    "12345678"

// === Timing ===
#define NETPIE_PUBLISH_INTERVAL 60000  // 60 seconds
#define TDS_READ_INTERVAL       1000   // 1 second
#define PH_READ_INTERVAL        1000   // 1 second

// === NTP ===
#define NTP_SERVER          "pool.ntp.org"
#define GMT_OFFSET_SEC      25200      // GMT+7 (Thailand)
```

---

## Installation

### 1. Clone/Download Project

### 2. Install PlatformIO

### 3. Configure NETPIE Credentials

แก้ไขไฟล์ `include/config.h`:

```cpp
#define NETPIE_CLIENT_ID    "your-client-id"
#define NETPIE_TOKEN        "your-token"
#define NETPIE_SECRET       "your-secret"
```

### 4. Build & Upload

```bash
pio run              # Build
pio run -t upload    # Upload
pio device monitor   # Serial Monitor
```

### 5. First-time WiFi Setup

1. ESP32 จะสร้าง Access Point ชื่อ **AquaponicsAP**
2. เชื่อมต่อด้วยมือถือหรือคอมพิวเตอร์
3. เปิด browser ไปที่ **192.168.4.1**
4. เลือก WiFi และใส่รหัสผ่าน
5. ESP32 จะ restart และเชื่อมต่อ WiFi

---

## Production Features

### OTA Updates

ระบบรองรับการอัพเดท firmware ผ่าน WiFi โดยไม่ต้องเชื่อมต่อ USB:

1. **Enable OTA** (default: enabled)
   ```cpp
   // ใน config.h
   #define OTA_ENABLED 1
   #define OTA_HOSTNAME "aquaponics-sensor"
   ```

2. **Upload via PlatformIO:**
   ```bash
   pio run -t upload --upload-port <IP_ADDRESS>
   ```

3. **หรือใช้ Arduino IDE:**
   - Tools → Port → Network Ports → `aquaponics-sensor.local`

### System Health Monitoring

ตรวจสอบสถานะระบบผ่าน Serial command `health`:

```
===== SYSTEM HEALTH =====
Uptime: 86400 seconds
Free Heap: 245678 bytes
Min Free Heap: 234567 bytes
Watchdog Resets: 0
WiFi Reconnects: 2
MQTT Reconnects: 1
Sensors OK: YES
=========================
```

### Factory Reset

**วิธีที่ 1: ปุ่ม BOOT**
- กดค้างปุ่ม BOOT 5 วินาทีตอน boot
- ระบบจะลบทุกการตั้งค่าและ restart

**วิธีที่ 2: Serial Command**
- พิมพ์ `reset` ใน Serial Monitor

**วิธีที่ 3: NETPIE**
- ส่ง `{"data":{"factoryReset":1}}` ไปที่ shadow

### Logging System

ระบบใช้ structured logging แบบ conditional:

```cpp
// Production mode (default)
#define LOG_LEVEL LOG_LEVEL_INFO  // ไม่แสดง debug logs

// Development mode
#define LOG_LEVEL LOG_LEVEL_DEBUG  // แสดงทุก logs
```

**Log Levels:**
- `ERROR` - Critical errors
- `WARN` - Warnings
- `INFO` - General information
- `DEBUG` - Debug messages (development only)

## Troubleshooting

### WiFi ไม่เชื่อมต่อ

- **Factory Reset:** กด Boot button 5 วินาทีตอน boot
- ตรวจสอบว่า WiFi เป็น 2.4GHz
- ตรวจสอบ log ผ่าน Serial Monitor

### pH อ่านค่าไม่ถูกต้อง

- Calibrate ใหม่ด้วย buffer pH 7.0 และ 4.0
- ตรวจสอบว่า probe ไม่แห้ง
- ตรวจสอบ calibration values: `ph` command

### NETPIE ไม่เชื่อมต่อ

- ตรวจสอบ credentials ใน `include/secrets.h`
- ตรวจสอบ internet connection
- ดู log ผ่าน Serial Monitor (`LOG_LEVEL_DEBUG`)

### Memory Issues

- ตรวจสอบ free heap: `health` command
- ถ้า free heap < 20KB ระบบจะแจ้งเตือน
- Restart ระบบถ้าจำเป็น

### OTA Update Failed

- ตรวจสอบว่า ESP32 เชื่อมต่อ WiFi แล้ว
- ตรวจสอบ OTA password (ถ้ามี)
- ใช้ USB upload เป็นทางเลือก

---

## License

MIT License

## Author

Chanakun - Aquaponics Project 2024-2026
