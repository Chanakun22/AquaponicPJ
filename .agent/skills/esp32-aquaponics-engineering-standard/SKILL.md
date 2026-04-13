---
name: esp32-aquaponics-engineering-standard
description: The comprehensive engineering handbook for the ESP32 Aquaponics Project. Contains strict coding standards, architectural patterns, and deployment checklists derived from project history.
---

# ESP32 Aquaponics Engineering Standards

This skill defines the **Mandatory Engineering Standards** for the Aquaponics Sensor & Controller project. Any code generated or modified must strictly adhere to these guidelines to ensure stability and maintainability.

---

## 1. Architectural Core Principles

### 1.1 The "No-Block" Policy (Critical)

The system runs on **FreeRTOS dual-core** with dedicated tasks. **Blocking code causes WDT Resets and Stuck Task alarms.**

- **FORBIDDEN:** `delay()`, `do...while` loops waiting for hardware, blocking I/O
- **MANDATORY:** Use `millis()` based state machines or non-blocking polling
- **ISR SAFETY:** Never call `Serial.print` or complex logic inside interrupt routines
- **Exception:** `vTaskDelay()` is acceptable for yielding within FreeRTOS tasks

### 1.2 FreeRTOS Task Layout

```
Core 1:  TaskSensors    → อ่านเซ็นเซอร์ 5 ตัว (millis-based polling)
         TaskControl    → ควบคุมไฟ (NeoPixel + Schedule) + Stuck Task Monitor
Core 0:  TaskNetworking → WiFi, MQTT (Netpie + Local), OTA, Telnet, CLI
```

### 1.3 WiFiClient / MQTT Timeout (สำคัญมาก!)

- **ต้องตั้ง `WiFiClient.setTimeout(5)`** ทุกครั้งที่สร้าง WiFiClient สำหรับ MQTT
- **ต้องตั้ง `PubSubClient.setSocketTimeout(5)`** ใน setup function
- ค่า default ของ WiFiClient คือ **~30 วินาที** → ถ้า broker unreachable จะ block ทั้ง task
- **ต้องเพิ่ม `systemTaskHeartbeat()` + `esp_task_wdt_reset()`** หลังทุก function ที่อาจ block

```cpp
// ✅ ถูกต้อง — ตั้ง timeout + heartbeat reset หลัง blocking call
void mqttSetup() {
    _wifiClient.setTimeout(5);      // 5 seconds TCP timeout
    _mqtt.setSocketTimeout(5);       // 5 seconds MQTT timeout
}

// ใน TaskNetworking loop:
netpieLoop();                        // อาจ block สูงสุด 5 วินาที
systemTaskHeartbeat(TASK_NETWORKING); // ⚠️ ต้องมี! ไม่งั้น stuck task alarm จะดัง
esp_task_wdt_reset();
```

### 1.4 Offline-Safe Design

ระบบต้องทำงานได้ 100% เมื่อไม่มี WiFi:

- **MQTT:** ห้ามพยายาม connect ถ้า `!wifiIsConnected()`
- **MQTT Reconnect:** ต้องใช้ **Exponential Backoff** (5s→10s→20s→...→60s max) ไม่ใช่ fixed interval
- **Telnet/OTA:** Skip เมื่อ WiFi offline — ไม่ต้องเรียก loop
- **NTP/getLocalTime():** ต้องมี timeout (10ms) — default blocks 5 วินาที
- **Log Queue:** ไม่ queue log เมื่อ MQTT ไม่ connected — ป้องกัน queue ล้น
- **Sensors:** ต้องทำงานอิสระจาก network — อ่านค่าได้แม้ไม่มีเน็ต

### 1.5 Sensor Polling Strategy

Sensors must NOT be read in every `loop()` iteration.

- **Pattern:** `millis()` based polled reading
- **Cache Strategy:** Functions like `readTemp()` return _last known good value_ instantly
- **Actual Read:** Happens only when the interval timer expires

```cpp
// ✅ Reference Implementation
void sensorLoop() {
    if (millis() - lastRead > INTERVAL) {
        lastRead = millis();
        cachedValue = readHardware();
    }
}
float getValue() { return cachedValue; } // Instant return
```

### 1.6 Communication Architecture

- **Dual-Stack MQTT:** System publishes to BOTH Netpie (Cloud) and Local Mosquitto (Pi)
- **JSON Keys:** Must be consistent across both — Format: `snake_case` (e.g., `water_temp`, `uptime_sec`)
- **Resiliency:** Network failure must **NOT** stop sensor logic

---

## 2. Code Organization Standards

### 2.1 File Structure

- `src/moduleName.cpp` + `include/moduleName.h`
- **Global Objects:** Define in `.cpp`, `extern` in `.h`
- **Configuration:** All pinning and constants in `include/config.h`

### 2.2 Naming Conventions

| Category         | Convention                                                  |
| ---------------- | ----------------------------------------------------------- |
| Global Variables | `_camelCase` with leading underscore (e.g., `_lastRunTime`) |
| Constants        | `UPPER_SNAKE_CASE` (e.g., `WIFI_TIMEOUT_MS`)                |
| Functions        | `moduleAction` (e.g., `wifiSetup`, `dhtRead`)               |
| JSON Keys        | `snake_case` (e.g., `water_temp`, `uptime_sec`)             |
| Strings (ESP32)  | Use `snprintf()` / C-strings, avoid `String` class          |

### 2.3 ⚠️ ก่อนเพิ่มตัวแปร/Constant ใหม่ ต้องค้นหาก่อนเสมอ!

**กฎเหล็ก:**

1.  **ก่อนสร้าง `#define` หรือ `const` ใหม่** → ค้นหาก่อนว่ามีอยู่แล้วหรือไม่
2.  **ก่อนใช้ค่า Hardcode** → ตรวจสอบว่ามี Constant ใน `config.h` แล้วหรือยัง
3.  **JSON Keys** → ต้องตรงกันทั้ง ESP32 (Publisher) และ Pi Server (Receiver)

```cpp
// ❌ ผิด - Hardcode ค่าซ้ำ
MDNS.begin("esp32-sensor");

// ✅ ถูก - ใช้ค่าจาก config.h
MDNS.begin(OTA_HOSTNAME);
```

---

## 3. Web & Frontend Standards (Pi Server)

### 3.1 Architecture

```
app.py (Flask + SocketIO)
├── WebSocket: dashboard_update event (ทุก 2s, server push)
├── REST API: /api/* endpoints (settings, health, logs, etc.)
├── Static: /static/ (fonts, icons, JS libs — downloaded by download_assets.sh)
└── Pages: *.html (ทุกหน้าใช้ base.css + header.js)

Shared Components:
├── base.css       → CSS Variables, Reset, Header, Card, Responsive base
├── header.js      → Nav bar + Net Stats + Logout + Hamburger menu
│                    ⚠️ Single Source of Truth สำหรับ nav — ห้ามเขียน nav CSS ในหน้าอื่น
└── sw.js (PWA)    → Service Worker cache (ต้อง bump version เมื่อเพิ่มไฟล์ใหม่)
```

### 3.2 UI/UX Rules

- **Framework:** Vanilla HTML/CSS/JS (Lightweight)
- **Theme:** Dark Mode using CSS Variables
- **Dashboard ใช้ WebSocket** (SocketIO) — ไม่ใช้ HTTP Polling
- **JS Libraries ต้อง local** — ห้ามใช้ CDN (เพื่อ offline mode) — ดาวน์โหลดผ่าน `download_assets.sh`

### 3.3 Nav CSS Rule (Critical)

- **Nav CSS** จัดการจาก `header.js` เท่านั้น
- **ห้ามเขียน** `.nav-bar` / `.nav-link` CSS ซ้ำในหน้า HTML อื่น
- **ทุกหน้าต้องมี:** `<link href="/base.css">` + `<script src="/header.js">` + `<div class="header"><div class="header-top">...`

### 3.4 ⚠️ Settings Integration Rule

**กฎเหล็ก:**

1.  **ห้าม** สร้าง Setting ใน UI โดยไม่มี Backend ที่รับค่าไปใช้งาน
2.  **ทุก Setting ต้องมี:**
    - UI Input (`settings.html`)
    - API Endpoint (`app.py`) ที่ Save/Load
    - Consumer Code ที่อ่านค่าไปใช้งานจริง

### 3.5 PWA & Caching

- เมื่อเพิ่ม/แก้ static files → ต้อง bump `CACHE_NAME` ใน `pwa/sw.js`
- `download_assets.sh` ต้องอัปเดตเมื่อเพิ่ม JS library ใหม่

---

## 4. ✅ VERIFICATION CHECKLIST

### 4.1 ESP32 Firmware

- [ ] `pio run` สำเร็จ ไม่มี error/warning
- [ ] ไม่มี `delay()` ใน production code
- [ ] WiFiClient มี `setTimeout()` ทุกตัว
- [ ] FreeRTOS tasks มี `systemTaskHeartbeat()` หลังทุก blocking call
- [ ] ค่า constant ไม่ hardcode ซ้ำ (ใช้จาก `config.h`)
- [ ] JSON keys ตรงกันทั้ง ESP32 และ Pi
- [ ] MQTT reconnect ใช้ exponential backoff
- [ ] getLocalTime() มี timeout parameter

### 4.2 Sensor Verification

| Sensor               | Expected Range | Check                              |
| -------------------- | -------------- | ---------------------------------- |
| Water Temp (DS18B20) | 15-35°C        | [ ] ค่าไม่ใช่ -127 หรือ NAN        |
| Air Temp (DHT22)     | 20-40°C        | [ ] ค่าไม่ใช่ NAN                  |
| Humidity (DHT22)     | 40-90%         | [ ] ค่าไม่ใช่ NAN                  |
| TDS                  | 200-800 ppm    | [ ] ค่าไม่ติดลบ                    |
| pH                   | 6.0-8.0        | [ ] ค่าเป็นเลขบวก (ถ้า -1 คือเสีย) |
| Light (BH1750)       | 0-65535 lux    | [ ] ค่าไม่ติดลบ                    |

### 4.3 Pi Server Web

- [ ] ทุกหน้ามี `header.js` + `.header` > `.header-top` structure
- [ ] ไม่มี nav CSS ซ้ำในไฟล์ HTML (ใช้จาก `header.js` เท่านั้น)
- [ ] JS libraries โหลดจาก `/static/js/` (ไม่ใช่ CDN)
- [ ] PWA `sw.js` cache version ถูก bump เมื่อเปลี่ยน static files
- [ ] `download_assets.sh` อัปเดตเมื่อเพิ่ม JS library ใหม่

### 4.4 Network & Stability

- [ ] WiFi RSSI > -70 dBm
- [ ] No "Interrupt WDT" or "Task WDT" ใน 5 นาที
- [ ] Free Heap > 100,000 Bytes
- [ ] WDT Resets = 0
- [ ] Stuck Task alarms = 0

### 4.5 Pi Deployment

```bash
# 1. Copy ไฟล์ไปยัง Pi
# 2. ดาวน์โหลด JS libraries (ถ้าเพิ่มใหม่)
cd ~/pi_server && bash download_assets.sh
# 3. Restart services
sudo systemctl restart aquaponics
```

---

## 5. 🚨 TROUBLESHOOTING GUIDE

| ปัญหา                       | สาเหตุ                          | วิธีแก้                                    |
| --------------------------- | -------------------------------- | ----------------------------------------- |
| Interrupt WDT Reset         | `delay()` หรืออ่าน Sensor รัวๆ    | ใช้ Cache + millis() timing               |
| Stuck Task alarm (30s)      | MQTT connect block > timeout     | ตรวจ WiFiClient.setTimeout(5)             |
| MQTT reconnect spam         | Fixed interval ไม่มี backoff     | ใช้ Exponential Backoff                   |
| WiFi Disconnect บ่อย        | RSSI อ่อน                        | ย้าย ESP ใกล้ Router                       |
| Sensor อ่านค่า NAN          | สายหลวม / Sensor เสีย            | เช็คสาย + เปลี่ยน Sensor                   |
| Dashboard ไม่โชว์ข้อมูล     | WebSocket ไม่ connect            | ตรวจ Mosquitto + Flask service             |
| Nav bar ไม่แสดง/ซ้ำ         | CSS override ใน HTML             | ลบ nav CSS ในหน้า — ใช้จาก header.js เท่านั้น |
| Free Heap ลดลงเรื่อยๆ       | Memory Leak (String)             | ใช้ `snprintf()` แทน String +              |
| getLocalTime block          | ไม่มี timeout parameter          | เพิ่ม timeout: `getLocalTime(&t, 10)`     |

---

## 6. ✈️ OTA UPDATE PROCEDURE

1.  **ตรวจสอบว่า ESP32 Online:** `ping aquaponics-sensor.local`
2.  **Build:** `pio run`
3.  **Upload OTA:** `pio run -e ota_upload -t upload`
4.  **Verify:** ดู Serial Log หรือ Dashboard ว่า Firmware Version ใหม่ถูกต้อง

---

## 7. 📝 DOCUMENTATION

- **ต้องอัปเดต `CHANGELOG.md`** หลังจบทุก task สำคัญ
- ดู skill: `project-documentation-standards` สำหรับ format

---

_Last Updated: 2026-03-30_
