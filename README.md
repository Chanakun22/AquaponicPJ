# 🌿 Smart Aquaponics Sensor System (ESP32-S3)

> **Version:** 2.3.0  
> **Platform:** ESP32-S3 (Arduino Framework / PlatformIO)

ระบบควบคุมและติตตามฟาร์มอควาโปนิกส์อัจฉริยะแบบ **Real-Time & 100% Non-Blocking**
ออกแบบมาให้ทำงานเสถียร ไม่ค้าง แม้ WiFi หลุด หรือเซ็นเซอร์มีปัญหา

---

## ✨ ความสามารถหลัก (Key Features)

### 🚀 1. ระบบทำงานต่อเนื่อง (Non-Blocking Architecture)

นี่คือหัวใจสำคัญของโปรเจกต์นี้:

- **Zero-Lag:** การเชื่อมต่อ WiFi หรือการอ่านค่าเซ็นเซอร์ จะ **ไม่หยุด** การทำงานของส่วนอื่น
- **Multitasking:** ระบบสามารถวัดค่า, ควบคุมไฟ, และส่งข้อมูล MQTT ได้พร้อมๆ กันเสมือนทำ Multithreading
- **Resilient:** ถ้า WiFi หลุด ระบบจะพยายามต่อใหม่ใน Background โดยที่การวัดค่ายังดำเนินต่อไป

### 📡 2. การเชื่อมต่อไร้สาย (Connectivity)

- **Auto WiFi Config:**
  - เปิดเครื่องครั้งแรก หรือย้ายสถานที่ -> ระบบปล่อย WiFi ชื่อ `AquaponicsAP`
  - ใช้มือถือเกาะ แล้วเด้งหน้าตั้งค่า WiFi อัตโนมัติ (Captive Portal)
- **Telnet Debugging:** ดูค่าการทำงานผ่าน WiFi (Port 23) ได้เหมือนต่อสาย USB สะดวกเวลาติดตั้งจริง
- **Cloud Dashboard:** ส่งข้อมูลขึ้น **NETPIE (MQTT)** เพื่อดูผ่านแอปได้จากทุกที่

### 🛠 3. เซ็นเซอร์และการควบคุม (Peripherals)

- **pH Sensor:** วัดค่าความเป็นกรด-ด่าง พร้อมระบบ Calibrate 2 จุด (pH 4.0/7.0)
- **TDS Meter:** วัดความเข้มข้นปุ๋ย (ppm) พร้อมชดเชยอุณหภูมิ (ATC)
- **Environment:** วัดอุณหภูมิน้ำ (DS18B20), อุณหภูมิ/ความชื้นอากาศ (DHT22)
- **Smart Light:** วัดแสง (BH1750) และคุมไฟปลูกพืชอัตโนมัติ (Auto Grow Light)

---

## 📂 โครงสร้างไฟล์และการทำงาน (Code Structure)

เพื่อให้เข้าใจและแก้ไขได้ง่าย โปรเจกต์แยกไฟล์ตามหน้าที่:

| ไฟล์ (File)        | หน้าที่ (Responsibility) | การทำงาน                                                  |
| :----------------- | :----------------------- | :-------------------------------------------------------- |
| `main.cpp`         | **ผู้จัดการใหญ่**        | เรียก `setup()` และวน `loop()` เพื่อสั่งงานทุกโมดูล       |
| `system.cpp`       | **ดูแลสุขภาพระบบ**       | จัดการ Memory, Watchdog, Uptime และ Factory Reset         |
| `wifiConn.cpp`     | **จัดการ WiFi**          | เชื่อมต่อ WiFi แบบ Non-blocking และดูแลหน้า Config Portal |
| `netpie.cpp`       | **สื่อสาร Cloud**        | เชื่อมต่อ MQTT และส่งข้อมูล sensor data ไป NETPIE         |
| `telnetServer.cpp` | **Debug ไร้สาย**         | สร้าง Server Port 23 สำหรับส่ง Log ออกทาง WiFi            |
| `*Sensor.cpp`      | **อ่านค่า**              | ไฟล์แยกสำหรับ sensor แต่ละตัว (อ่าน->กรอง->เฉลี่ยค่า)     |

### ⚙️ หลักการทำงานของ Code (How it works)

**1. Setup Phase (เตรียมความพร้อม):**

```cpp
void setup() {
    // 1. เริ่มระบบ System & Watchdog (กันค้าง)
    // 2. เริ่ม WiFi Manager (ถ้าไม่มีเน็ต -> เปิด AP รอ)
    // 3. เริ่ม Telnet & OTA
    // 4. เริ่ม Sensor ทั้งหมด
}
```

**2. Loop Phase (วนทำงานตลอดเวลา):**
หัวใจคือการ **"แวะดู"** (Polling) ไม่ใช่การ **"รอ"** (Blocking):

```cpp
void loop() {
    systemLoop();    // เช็คสุขภาพระบบ
    wifiLoop();      // ดูแล WiFi (หลุดเชื่อมใหม่, มีคนเข้า Portal?)
    telnetLoop();    // มีใคร Telnet เข้ามาดู log ไหม?
    netpieLoop();    // ถึงเวลาส่งข้อมูลขึ้น Cloud หรือยัง?

    // --- โซนเซ็นเซอร์ ---
    // อ่านค่า -> Validate (กรองค่าเพี้ยน) -> เก็บใส่ตัวแปร Global
    // (ทำวนไปเรื่อยๆ)
}
```

---

## 🔌 คู่มือการใช้งาน (User Guide)

### 1. การติดตั้งครั้งแรก

1. อัพโหลดโค้ดลงบอร์ด ESP32-S3
2. เมื่อ LED สถานะติด -> ใช้มือถือค้นหา WiFi ชื่อ `AquaponicsAP`
3. กดเชื่อมต่อ -> หน้าตั้งค่า WiFi จะเด้งขึ้นมา
4. เลือก WiFi บ้าน -> ใส่รหัส -> กด Save -> บอร์ดจะรีบูตและเชื่อมต่อเอง

### 2. การดู Log การทำงาน (Debug)

**แบบต่อสาย:** เปิด Serial Monitor (Baud 115200)
**แบบไร้สาย (Telnet):**

1. หา IP ของบอร์ด (ดูจาก Router หรือตอนต่อ Serial ครั้งแรก)
2. ใช้แอป **"Serial WiFi Terminal"** (Android) หรือ **Putty** (PC)
3. Connect ไปที่ IP ของบอร์ด, Port **23**
4. ใส่รหัสผ่าน: `admin123` (แก้ได้ใน `include/secrets.h`)
5. จะเห็นหน้าจอ Log เหมือนต่อสายทุกประการ

### 3. การ Calibrate เซ็นเซอร์ pH

1. ต่อสาย Serial หรือ Telnet
2. จุ่มหัววัดในน้ำยา **pH 7.0** -> พิมพ์คำสั่ง `cal7` -> รอจนเสร็จ
3. ล้างหัววัด -> จุ่มในน้ำยา **pH 4.0** -> พิมพ์คำสั่ง `cal4` -> รอจนเสร็จ
4. พิมพ์ `ph` เพื่อทดสอบค่าปัจจุบัน

### 4. ปุ่ม Reset (Factory Reset)

หากย้ายสถานที่ หรือ WiFi เปลี่ยน:

- **กดปุ่ม BOOT ค้างไว้ 5-10 วินาที** (จนกว่าจะเห็น Log ว่า Resetting...)
- ระบบจะล้างค่า WiFi ทั้งหมดและรีบูตเข้าโหมด AP ใหม่

---

## � Pin Map (การต่อสาย)

| อุปกรณ์ (Device)  | ขา ESP32 (Pin) | หมายเหตุ                 |
| :---------------- | :------------: | :----------------------- |
| **Status LED**    |    GPIO 48     | มีอยู่แล้วบนบอร์ด DevKit |
| **DS18B20 (น้ำ)** |     GPIO 4     | ต้องต่อ R 4.7k Pull-up   |
| **DHT22 (อากาศ)** |     GPIO 5     |                          |
| **pH Sensor**     |     GPIO 6     | Analog Input             |
| **TDS Sensor**    |     GPIO 1     | Analog Input             |
| **BH1750 (แสง)**  | SDA: 8, SCL: 9 | I2C                      |
| **Relay (ไฟ)**    |    GPIO 10     | คุมไฟ Grow Light         |

---

_Developed for Smart Agriculture Project_
