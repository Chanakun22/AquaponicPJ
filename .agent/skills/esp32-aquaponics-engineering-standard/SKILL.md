---
name: esp32-aquaponics-engineering-standard
description: The comprehensive engineering handbook for the ESP32 Aquaponics Project. Contains strict coding standards, architectural patterns, and deployment checklists derived from project history.
---

# ESP32 Aquaponics Engineering Standards

This skill defines the **Mandatory Engineering Standards** for the Aquaponics Sensor & Controller project. Any code generated or modified must strictly adhere to these guidelines to ensure stability (prevent WDT resets) and maintainability.

---

## 1. Architectural Core Principles

### 1.1 The "No-Block" Policy (Critical)

The system runs on a single-core loop with WiFi/MQTT background tasks. **Blocking code causes WDT Resets.**

- **FORBIDDEN:** `delay()`, `do...while` loops waiting for hardware, long `Serial.print`.
- **MANDATORY:** Use `millis()` based state machines or non-blocking polling.
- **ISR SAFETY:** Never call `Serial.print` or complex logic inside `ICACHE_RAM_ATTR` interrupt routines.

### 1.2 Sensor Polling Strategy

Sensors must NOT be read in every `loop()` iteration.

- **Pattern:** polled_reading
- **Implementation:**
  - `loop()` calls `sensorLoop()`.
  - `sensorLoop()` checks `millis()`.
  - **Cache Strategy:** Functions like `readTemp()` must return the _last known good value_ instantly, not perform a new read.
  - **Actual Read:** Happens only when the interval timer expires.

```cpp
// ✅ Reference Implementation
void dhtLoop() {
    if (millis() - lastRead > 2000) {
        lastRead = millis();
        cachedTemp = dht.readTemperature(); // Updates cache
    }
}
float getTemp() { return cachedTemp; } // Instant return
```

### 1.3 Communication Architecture

- **Dual-Stack MQTT:** System must publish to BOTH Netpie (Cloud) and Local Mosquitto (Pi).
- **JSON Structure:** Keys must be consistent across both.
  - Format: `snake_case` (e.g., `water_temp`, `uptime_sec`).
- **Resiliency:** Network failure must **NOT** stop sensor logic.

---

## 2. Code Organization Standards

### 2.1 File Structure

- `src/moduleName.cpp` + `include/moduleName.h`
- **Global Objects:** Define in `.cpp`, `extern` in `.h`.
- **Configuration:** All pinning and constants in `include/config.h`.

### 2.2 Naming Conventions

- **Global Variables:** `_camelCase` with leading underscore (e.g., `_lastRunTime`).
- **Constants:** `UPPER_SNAKE_CASE` (e.g., `WIFI_TIMEOUT_MS`).
- **Functions:** `moduleAction` (e.g., `wifiSetup`, `dhtRead`).

### 2.3 ⚠️ ก่อนเพิ่มตัวแปร/Constant ใหม่ ต้องค้นหาก่อนเสมอ!

**ปัญหาที่เคยเกิด:** ใช้ตัวแปรซ้ำชื่อแต่ค่าไม่ตรงกัน ทำให้ระบบทำงานผิดพลาด

**กฎเหล็ก:**

1.  **ก่อนสร้าง `#define` หรือ `const` ใหม่** → ค้นหาก่อนว่ามีอยู่แล้วหรือไม่
2.  **ก่อนใช้ค่า Hardcode** → ตรวจสอบว่ามี Constant ใน `config.h` แล้วหรือยัง
3.  **JSON Keys** → ต้องตรงกันทั้ง ESP32 (Publisher) และ Pi Server (Receiver)

```bash
# ตัวอย่างการค้นหาก่อนใช้งาน
grep -rn "MQTT_TOPIC" include/ src/
grep -rn "OTA_HOSTNAME" include/ src/
grep -rn "water_temp" pi_server/ src/
```

**ตัวอย่างที่ผิด vs ถูก:**

```cpp
// ❌ ผิด - Hardcode ค่าซ้ำ (อาจไม่ตรงกับที่อื่น)
MDNS.begin("esp32-sensor");
client.publish("aquaponics/data", payload);

// ✅ ถูก - ใช้ค่าจาก config.h
MDNS.begin(OTA_HOSTNAME);
client.publish(MQTT_TOPIC_SENSORS, payload);
```

**Checklist ก่อน Commit:**

- [ ] ค้นหา `#define` หรือ `const` ที่เกี่ยวข้องแล้ว
- [ ] ไม่มี Hardcode ค่าที่ควรใช้จาก `config.h`
- [ ] mDNS Hostname ตรงกันทุกที่ (OTA, Local MQTT)
- [ ] MQTT Topics ตรงกันทั้ง ESP32 และ Pi
- [ ] JSON Keys ตรงกันทั้ง Sender และ Receiver

---

## 3. Web & Frontend Standards (Pi Server)

### 3.1 UI/UX Philosophy

- **Framework:** Vanilla HTML/CSS/JS (Lightweight).
- **Theme:** "Neon Dark Mode" using CSS Variables (`--accent-color`, `--bg-color`).
- **Responsiveness:** Mobile-first layout.

### 3.2 Real-time Updates

- **Mechanism:** Client-side polling (`setInterval`).
- **Frequency:** Every 2-5 seconds.
- **UX:** No full page reloads. Use JavaScript to update DOM elements by ID.
- **Visual Feedback:** Pulse animations for live status/heartbeat.

### 3.3 Graphing

- **Engine:** Chart.js.
- **Timezone:** Display strictly in **Thai Time (UTC+7)**, converted from Server UTC.
- **History:** Support multiple ranges (24H, 3 Days) via distinct API queries.

---

## 4. ✅ PRE-DEPLOYMENT CHECKLIST

### 4.1 ESP32 Hardware Checks

- [ ] **Power Supply:** ใช้ USB หรือแหล่งจ่ายที่เสถียร (5V/2A ขึ้นไป)
- [ ] **Wiring:** ตรวจสอบการต่อสาย Sensor ทั้งหมด (ไม่หลวม ไม่ลัดวงจร)
- [ ] **GPIO Conflicts:** ไม่มี Pin ที่ใช้ซ้อนกัน (ตรวจสอบใน `config.h`)
- [ ] **Status LED:** กระพริบปกติ (ไม่ค้าง)

### 4.2 Sensor Verification

| Sensor               | Expected Range | Check                              |
| -------------------- | -------------- | ---------------------------------- |
| Water Temp (DS18B20) | 15-35°C        | [ ] ค่าไม่ใช่ -127 หรือ NAN        |
| Air Temp (DHT22)     | 20-40°C        | [ ] ค่าไม่ใช่ NAN                  |
| Humidity (DHT22)     | 40-90%         | [ ] ค่าไม่ใช่ NAN                  |
| TDS                  | 200-800 ppm    | [ ] ค่าไม่ติดลบ                    |
| pH                   | 6.0-8.0        | [ ] ค่าเป็นเลขบวก (ถ้า -1 คือเสีย) |
| Light (BH1750)       | 0-65535 lux    | [ ] ค่าไม่ติดลบ                    |

### 4.3 Network Checks

- [ ] **WiFi Connected:** Serial Monitor โชว์ IP Address
- [ ] **RSSI:** สัญญาณ WiFi > -70 dBm (ถ้าอ่อนกว่านี้อาจหลุดบ่อย)
- [ ] **NETPIE MQTT:** สถานะ `CONNECTED` / ดู Shadow ใน NETPIE Console
- [ ] **Local MQTT (Pi):** ดู Log ของ Mosquitto ว่า ESP32 Connect แล้ว
- [ ] **mDNS:** ทดสอบ `ping aquaponics-sensor.local` (ต้องตอบ)

### 4.4 Watchdog & Stability

- [ ] **No Interrupt WDT:** เปิด Serial Monitor ค้างไว้ 5 นาที ต้องไม่มี "Interrupt WDT" หรือ "Task WDT"
- [ ] **Uptime:** ตรวจสอบด้วยคำสั่ง `test` ใน Serial ว่า Uptime เพิ่มขึ้นเรื่อยๆ
- [ ] **Free Heap:** > 100,000 Bytes (ถ้าต่ำกว่า 50K อาจ Crash)
- [ ] **WDT Resets:** ตัวเลข Watchdog Resets ควรเป็น 0

### 4.5 Pi Server Checks

- [ ] **SSH:** เข้าได้ผ่าน `ssh pi@raspberrypi.local`
- [ ] **Mosquitto Running:** `sudo systemctl status mosquitto` (active/running)
- [ ] **Aquaponics Service:** `sudo systemctl status aquaponics` (active/running)
- [ ] **Tailscale (VPN):** `tailscale status` (connected)
- [ ] **Dashboard:** เปิด `http://<IP>` ได้ปกติ
- [ ] **Graphs:** ข้อมูลกราฟย้อนหลังโชว์ถูกต้อง (Thai Time)
- [ ] **Logs:** หน้า Full Logs แสดงข้อมูลและ Auto-Refresh

### 4.6 ESP32 Serial Commands (Self-Test)

พิมพ์คำสั่งเหล่านี้ใน Serial Monitor:

| Command   | Expected Result                   |
| --------- | --------------------------------- |
| `help`    | แสดงรายการคำสั่งทั้งหมด           |
| `status`  | แสดงค่า Sensor ทั้งหมด            |
| `test`    | รัน Diagnostic ครบ (ทุกบรรทัด OK) |
| `health`  | Uptime, Heap, WDT Resets = 0      |
| `wifi`    | Connected, IP, RSSI               |
| `mqtt`    | Connected to NETPIE               |
| `version` | Firmware Version ตรงกับที่ Upload |

---

## 5. 🚨 TROUBLESHOOTING GUIDE

| ปัญหา                   | สาเหตุที่พบบ่อย                    | วิธีแก้                                   |
| ----------------------- | ---------------------------------- | ----------------------------------------- |
| Interrupt WDT Reset     | มี `delay()` หรืออ่าน Sensor รัวๆ  | ใช้ Cache + millis() timing               |
| WiFi Disconnect บ่อย    | RSSI อ่อน หรือ Router ไกล          | ย้าย ESP ใกล้ Router / ใช้ Antenna ภายนอก |
| Sensor อ่านค่า NAN      | สายหลวม หรือ Sensor เสีย           | เช็คสาย + ลองเปลี่ยน Sensor               |
| Dashboard ไม่โชว์ข้อมูล | MQTT ไม่ Connect                   | ตรวจสอบ Mosquitto และ Firewall            |
| กราฟไม่มีข้อมูล         | Database ว่าง                      | รอ 1 นาทีให้ระบบเก็บข้อมูลก่อน            |
| Free Heap ลดลงเรื่อยๆ   | Memory Leak (String concatenation) | ใช้ `snprintf()` แทน String +             |

---

## 6. 📁 PROJECT FILE STRUCTURE

```
test/
├── include/
│   ├── config.h          # Constants, Pins, Timings
│   ├── secrets.h          # WiFi, API Keys (GITIGNORED)
│   ├── *.h                # Module headers
├── src/
│   ├── main.cpp           # Entry point
│   ├── *.cpp              # Module implementations
├── pi_server/
│   ├── app.py             # Flask Server (GITIGNORED)
│   ├── index.html         # Dashboard (GITIGNORED)
│   ├── graphs.html        # Historical Graphs
│   ├── full_logs.html     # Log Viewer
├── platformio.ini         # PlatformIO Config
├── PI_COMMANDS.md         # Pi Cheat Sheet
└── .agent/skills/         # AI Skills (this file)
```

---

## 7. ✈️ OTA UPDATE PROCEDURE

1.  **ตรวจสอบว่า ESP32 Online:** `ping aquaponics-sensor.local`
2.  **Build:** `pio run`
3.  **Upload OTA:** `pio run -t upload --upload-port aquaponics-sensor.local`
4.  **Verify:** ดู Serial Log หรือ Dashboard ว่า Firmware Version ใหม่ถูกต้อง

---

## 8. 🔄 DAILY MAINTENANCE

- [ ] ตรวจสอบ Dashboard ว่าแสดงผลปกติ
- [ ] ดู Graphs ว่าข้อมูลไม่หายไปกลางคัน
- [ ] ดู Full Logs ว่าไม่มี ERROR ซ้ำๆ
- [ ] ตรวจสอบค่า pH และ TDS อยู่ในช่วงที่เหมาะสม

---

_Last Updated: 2026-02-06 by AI Assistant_
