# 🐟 Smart Aquaponics Controller (ESP32-S3)

**ระบบควบคุมและตรวจสอบคุณภาพน้ำอัจฉริยะสำหรับฟาร์มอควาโปนิกส์**
พัฒนาด้วยมาตรฐาน **Production-Grade Firmware** บน PlatformIO เน้นความเสถียร (Reliability), การทำงานแบบไม่บล็อก (Non-Blocking), และการบำรุงรักษาง่าย (Maintainability)

---

## 🏗️ สถาปัตยกรรมระบบ (System Architecture)

โปรเจกต์นี้ถูกออกแบบตามมาตรฐาน **Modular Skill Standard** เพื่อความยืดหยุ่นและรองรับการขยายตัวในอนาคต:

### 1. Non-Blocking Core (หัวใจสำคัญ)
ระบบทำงานด้วย **Async State Machine** เต็มรูปแบบ **ไม่มีการใช้คำสั่ง `delay()`** ใน Loop หลัก ทำให้ระบบสามารถทำงานหลายอย่างพร้อมกันได้ (Multitasking) โดยไม่สะดุด
- **System Loop**: ทำงานด้วยความเร็วสูงสุด (Loop Latency ต่ำกว่า 10ms)
- **Watchdog Timer (WDT)**: มีระบบเฝ้าระวังความผิดพลาด หากระบบค้างเกิน 30 วินาที จะทำการรีเซ็ตตัวเองอัตโนมัติ

### 2. Modular Design (การออกแบบโมดูล)
แยกส่วนการทำงานชัดเจน (Separation of Concerns) ระหว่าง Header (`include/`) และ Implementation (`src/`):
- **Sensors**: `phSensor`, `tdsSensor`, `dhtSensor`, `tempSensor` แยกกันเป็นอิสระ
- **Connectivity**: `wifiConn` (Auto-Reconnect), `netpie` (MQTT Cloud)
- **System**: `system` (Health Check, Persistence), `logger` (Centralized Logging)

---

## ✨ ฟีเจอร์หลัก (Key Features)

### 📊 Real-time Monitoring Dashboard
เข้าถึงผ่าน Web Browser ภายในวงแลน (`http://<ip>/monitor`)
- **Sensor Cards**: แสดงค่า pH, TDS, Temp, Humidity, Light แบบ Real-time
- **Hardware Stats**:
  - **🧠 RAM Usage**: หลอดแสดงปริมาณการใช้หน่วยความจำ
  - **🌡️ CPU Temp**: อุณหภูมิภายในชิป ESP32-S3 (แจ้งเตือนเมื่อ Overheat)
- **System Health**: อัปไทม์ (Uptime), สัญญาณ WiFi (RSSI), ประวัติการเชื่อมต่อหลุด (Reconnect Logs)

### 📡 Connectivity (การเชื่อมต่อ)
- **WiFi Manager**: ระบบ Captive Portal สำหรับตั้งค่า WiFi ครั้งแรก (ไม่ต้อง Hardcode ชื่อ WiFi)
- **NETPIE Integration**: ส่งข้อมูลขึ้น Cloud (MQTT) อัตโนมัติเมื่อเชื่อมต่อเน็ตสำเร็จ
- **OTA Updates**: รองรับการอัปเดต Firmware ไร้สายผ่าน WiFi
- **Telnet Console**: ดีบักระบบระยะไกลผ่าน Port 23

### 🛡️ Reliability (ความน่าเชื่อถือ)
- **Error Handling**: กรองค่าเซ็นเซอร์ที่ผิดปกติ (Outlier Rejection)
- **Auto Recovery**: ระบบพยายามเชื่อมต่อ WiFi และ MQTT ใหม่ทันทีที่หลุด โดยไม่กระทบการอ่านค่าเซ็นเซอร์
- **Persisted config**: บันทึกค่า Calibrate และสถิตระบบลงหน่วยความจำถาวร (NVS)

---

## 🛠️ การใช้งานและการตั้งค่า (Operation Guide)

### 1. การเริ่มต้น (First Time Setup)
1. จ่ายไฟเข้าบอร์ด ไฟสถานะจะกระพริบ
2. หากยังไม่เคยตั้งค่า ให้เชื่อมต่อ WiFi ชื่อ **`Aquaponics-Setup`**
3. เข้า Browser ไปที่ `192.168.4.1` เพื่อตั้งค่า WiFi บ้าน
4. บอร์ดจะรีบูทและเชื่อมต่ออัตโนมัติ

### 2. การ Calibrate เซ็นเซอร์ (Calibration)
รองรับการ Calibrate ผ่าน Telnet/Serial Command:
- **pH Sensor**: จุ่มน้ำยาบัฟเฟอร์ พิมพ์ `ph cal <ค่า>` (เช่น `ph cal 4.0`)
- **Factory Reset**: หากต้องการล้างค่าทั้งหมด กดปุ่ม BOOT ค้าง 10 วินาที หรือพิมพ์ `factory_reset`

### 3. API สำหรับนักพัฒนา
ระบบเปิดพอร์ต HTTP API สำหรับดึงข้อมูลไปใช้ต่อ:
- `GET /api/sensors`: ค่าเซ็นเซอร์ทั้งหมด (JSON)
- `GET /api/health`: สถานะระบบ (Heap, Temp, Reconnects)

---

## 📦 Technical Stack
- **MCU**: ESP32-S3 DevKitC-1
- **Framework**: Arduino Core for ESP32 (v2.0.11 via PlatformIO)
- **Libraries**:
  - `ESPAsyncWebServer` & `AsyncTCP`: สำหรับ Web Server ประสิทธิภาพสูง
  - `PubSubClient`: สำหรับ MQTT (NETPIE)
  - `ArduinoJson 6`: สำหรับจัดการข้อมูล JSON
  - `WiFiManager`: สำหรับจัดการการเชื่อมต่อ

---
*Verified and Documented by Antigravity AI Agent*
