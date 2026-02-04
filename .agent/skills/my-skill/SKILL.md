---
name: esp32-aquaponics-module
description: A skill for creating new sensor modules and components for the ESP32 Aquaponics System. Use this when adding new sensors, actuators, or features to the project.
---

# ESP32 Aquaponics Module Skill

You are an expert in ESP32 development, PlatformIO, and embedded systems. When this skill is activated, you must strictly follow these rules:

## 1. File Structure Standards

- Every new module must have **separate header (.h) and implementation (.cpp) files**
- Header files go in `include/` directory
- Implementation files go in `src/` directory
- Use the existing naming convention: `moduleName.h` and `moduleName.cpp`

## 2. Non-Blocking Architecture

- **CRITICAL:** Never use `delay()` in the main loop - use `millis()` based timing instead
- Implement a `moduleSetup()` function for initialization
- Implement a `moduleLoop()` function that gets called from `main.cpp`
- Use state machines for complex operations that require waiting

```cpp
// ✅ Correct Non-blocking pattern
static unsigned long lastReadTime = 0;
if (millis() - lastReadTime >= READ_INTERVAL) {
    lastReadTime = millis();
    // Do sensor reading here
}

// ❌ Wrong - Never use delay()
delay(1000);
```

## 3. Logging Standards

- Use the project's logging macros: `LOG_DEBUG()`, `LOG_INFO()`, `LOG_WARN()`, `LOG_ERROR()`
- Always include function context in logs: `[ModuleName]`
- Log important state changes and errors

## 4. Configuration

- Add configurable parameters to `include/config.h`
- Add sensitive data (keys, passwords) to `include/secrets.h`
- Use `#define` with clear naming: `MODULENAME_PARAMETER_NAME`

## 5. NETPIE Integration

- If the module produces sensor data, add it to the shadow update in `netpie.cpp`
- Follow the existing JSON structure format
- Use consistent key naming in lowercase with underscores

## 6. Code Quality

- Include proper header guards: `#ifndef MODULENAME_H` / `#define MODULENAME_H`
- Add descriptive comments in Thai for educational purposes
- Implement input validation and error handling
- Use `extern` for global data access across files

## 7. Production Standards

### 7.1 Non-Blocking Requirements (Critical for Production)

- **ABSOLUTELY NO `delay()` ALLOWED** - ใช้ `millis()` based timing เท่านั้น
- Use **State Machines** for multi-step operations (e.g., sensor calibration, connection retry)
- Implement **Async I/O patterns** - never wait synchronously for network/sensor responses
- Consider **FreeRTOS Tasks** for CPU-intensive operations that might block the main loop
- Always **yield to other tasks** using `vTaskDelay(1)` or `yield()` in long loops
- Implement **timeout mechanisms** for all external communications

```cpp
// ✅ Production Non-blocking State Machine Pattern
enum SensorState { IDLE, READING, PROCESSING, ERROR };
static SensorState state = IDLE;
static unsigned long stateTimer = 0;

void sensorLoop() {
    switch (state) {
        case IDLE:
            if (millis() - stateTimer >= READ_INTERVAL) {
                startAsyncRead();
                state = READING;
                stateTimer = millis();
            }
            break;
        case READING:
            if (isReadComplete()) {
                state = PROCESSING;
            } else if (millis() - stateTimer > TIMEOUT_MS) {
                state = ERROR;  // Timeout - graceful handling
            }
            break;
        case PROCESSING:
            processData();
            state = IDLE;
            stateTimer = millis();
            break;
        case ERROR:
            handleError();
            state = IDLE;
            stateTimer = millis();
            break;
    }
}
```

### 7.2 Error Handling & Reliability

- Ensure code is **production-ready** with proper error handling and recovery
- Implement **Watchdog Timer (WDT)** feeding in long-running operations to prevent system hangs
- Use **OTA (Over-The-Air)** compatible code structure - avoid blocking operations during updates
- Optimize **memory usage** - prefer stack allocation over heap when possible
- Add **graceful degradation** - if a sensor fails, system should continue operating with other sensors
- Use **LOG_LEVEL** appropriately: DEBUG for development, INFO or WARN for production
- Implement **retry mechanisms** with exponential backoff for network operations
- Consider **power consumption** for battery-powered deployments

```cpp
// ✅ Production-ready error handling
bool readSensor() {
    static int failCount = 0;
    float value = sensor.read();

    if (isnan(value) || value < MIN_VALID || value > MAX_VALID) {
        failCount++;
        LOG_WARN("[Sensor] Invalid reading #%d", failCount);
        if (failCount >= MAX_RETRIES) {
            LOG_ERROR("[Sensor] Max retries exceeded, using last known good value");
        }
        return false;
    }
    failCount = 0;  // Reset on success
    return true;
}
```

## 8. Response Language

- **Crucial:** Explain your technical decisions in **Thai language** so the user can understand the implementation
- Keep the tone professional, encouraging, and easy to understand for beginners
- Reference existing modules (like `phSensor.cpp`, `TdsSensor.cpp`) as examples when relevant

## 9. Web Dashboard Standards

- ใช้ **CSS Variables** สำหรับ Theme (สี, ขนาด) เพื่อให้ปรับแต่งง่าย
- JavaScript ต้องรองรับ **Offline Fallback** - แสดง "Disconnected" เมื่อ API ไม่ตอบ
- ใช้ **Polling interval ไม่ต่ำกว่า 2 วินาที** เพื่อป้องกัน ESP32 Overload
- แสดง **Loading State** ขณะรอข้อมูล (spinner หรือ skeleton)
- รองรับ **Responsive Design** สำหรับหน้าจอมือถือ

```javascript
// ✅ Correct polling pattern with error handling
async function fetchData() {
  try {
    const response = await fetch("/api/sensors");
    if (!response.ok) throw new Error("API Error");
    updateUI(await response.json());
  } catch (error) {
    showDisconnectedState();
  }
}
setInterval(fetchData, 2000); // ไม่ต่ำกว่า 2 วินาที
```

## 10. API Design Guidelines

- Endpoint ต้องใช้ `/api/` prefix เสมอ (เช่น `/api/sensors`, `/api/health`)
- Response เป็น **JSON** ทุกครั้ง พร้อม `Content-Type: application/json`
- ต้องมี **CORS headers** สำหรับ Cross-origin requests
- ใช้ **snake_case** สำหรับ key names (เช่น `water_temp`, `ph_value`)
- Return **HTTP status codes** ที่เหมาะสม (200 OK, 400 Bad Request, 500 Error)

```cpp
// ✅ Correct API response format
String json = "{";
json += "\"water_temp\":" + String(temp, 1);
json += ",\"ph_value\":" + String(ph, 2);
json += ",\"timestamp\":" + String(millis());
json += "}";
request->send(200, "application/json", json);
```

## 11. Sensor Data Validation

- ต้องตรวจสอบ `isnan()` ทุกครั้งก่อนใช้ค่า Sensor
- กำหนด **MIN/MAX ที่เป็นไปได้** ของแต่ละ Sensor (เช่น pH 0-14, Temp -10 to 100°C)
- ใช้ **"Last Known Good Value"** เมื่ออ่านค่าผิดพลาด
- นับจำนวนครั้งที่ล้มเหลว (`failCount`) เพื่อแจ้งเตือน
- ใช้ **Median Filter** สำหรับ Sensor ที่มี Noise สูง (เช่น pH, TDS)

```cpp
// ✅ Correct sensor validation
float validateReading(float value, float min, float max, float lastGood) {
    if (isnan(value) || value < min || value > max) {
        return lastGood;  // ใช้ค่าเดิมที่ถูกต้อง
    }
    return value;
}
```

## 12. Memory Best Practices

- ใช้ `static` สำหรับ buffer ที่ใช้ซ้ำ เพื่อลด **Heap Fragmentation**
- ใช้ `strlcpy()` แทน `strcpy()` เพื่อป้องกัน **Buffer Overflow**
- ตรวจสอบ `ESP.getFreeHeap()` อย่างน้อยทุก 5 นาที
- **Log เตือน** เมื่อ Free Heap < 20KB (อาจ crash ได้)
- หลีกเลี่ยง `String` concatenation แบบซ้ำๆ (กิน heap) ใช้ `snprintf()` แทน

```cpp
// ✅ Safe string handling
static char buffer[128];  // Static buffer - ไม่กิน heap ซ้ำ
snprintf(buffer, sizeof(buffer), "Temp: %.1f, pH: %.2f", temp, ph);

// ❌ Avoid - กิน heap ทุกครั้ง
String msg = "Temp: " + String(temp) + ", pH: " + String(ph);
```

## 13. Pre-Deploy Checklist

ก่อน Deploy ต้องตรวจสอบทุกข้อ:

### Build & Compile

- [ ] `pio run` สำเร็จไม่มี Error และไม่มี Warning สำคัญ
- [ ] ขนาด Flash ไม่เกิน 80% (เผื่อ OTA)

### Connectivity

- [ ] WiFi Reconnect ทำงานปกติ (ลอง Disconnect Router แล้วต่อใหม่)
- [ ] MQTT Reconnect ทำงานปกติ
- [ ] OTA Update ทำงานปกติ

### Reliability

- [ ] Watchdog Reset ทำงาน (ลองใส่ `while(1){}` แล้วดูว่า Reset หรือไม่)
- [ ] Factory Reset Button ทำงาน
- [ ] Free Heap ไม่ลดลงเรื่อยๆ (Memory Leak)

### Sensors & UI

- [ ] Sensor ทุกตัวอ่านค่าถูกต้อง
- [ ] Dashboard แสดงผลครบทุกช่อง
- [ ] API ตอบกลับถูกต้อง (/api/sensors, /api/health)

## 14. Code Consistency Verification (สำคัญมาก!)

ก่อนสร้างหรือแก้ไข Module ใหม่ ต้องตรวจสอบความสอดคล้องของโค้ดทั้งระบบ:

### 14.1 Constants & Defines Consistency

ต้องใช้ค่าที่กำหนดใน `config.h` เท่านั้น **ห้าม hardcode ค่าซ้ำ**:

```cpp
// ❌ ผิด - Hardcode ค่าซ้ำ (เกิดปัญหา hostname ไม่ตรงกัน)
MDNS.begin("esp32-sensor");  // ใครจะไปรู้ว่า OTA ใช้ชื่ออื่น?

// ✅ ถูก - ใช้ค่าจาก config.h เดียวกัน
MDNS.begin(OTA_HOSTNAME);    // OTA_HOSTNAME = "aquaponics-sensor"
```

### 14.2 ก่อนเพิ่มโค้ดใหม่ ต้องค้นหาก่อน

เมื่อจะใช้ค่า Constant หรือเรียกใช้ Library function ใหม่ **ต้อง grep ค้นหาก่อน**:

```bash
# ตัวอย่าง: ก่อนใช้ MDNS.begin() ให้ค้นหาว่ามีที่ไหนใช้อยู่แล้วบ้าง
grep -r "MDNS.begin" src/
grep -r "OTA_HOSTNAME" include/ src/
```

### 14.3 Checklist ก่อน Commit โค้ดใหม่

- [ ] ค้นหา `#define` หรือ `const` ที่เกี่ยวข้องก่อนใช้ค่าใหม่
- [ ] ไม่มีการ hardcode ค่าที่ควรจะใช้จาก `config.h`
- [ ] ตรวจสอบว่า mDNS hostname ตรงกันทุกที่ (OTA, MQTT, Web)
- [ ] ตรวจสอบว่า MQTT topics ตรงกันทั้ง Publisher และ Subscriber
- [ ] ตรวจสอบว่า JSON keys ตรงกันทั้ง Sender และ Receiver

### 14.4 Common Pitfalls (หลุมพรางที่พบบ่อย)

| ปัญหา                   | สาเหตุ                  | วิธีป้องกัน                                 |
| ----------------------- | ----------------------- | ------------------------------------------- |
| OTA หาบอร์ดไม่เจอ       | mDNS hostname ไม่ตรงกัน | ใช้ `OTA_HOSTNAME` ทุกที่                   |
| MQTT ไม่ได้ข้อมูล       | Topic ไม่ match         | ใช้ `#define MQTT_TOPIC_*`                  |
| Dashboard ข้อมูลหาย     | JSON key ผิด            | ใช้ `snake_case` เหมือนกันทั้ง ESP32 และ Pi |
| WiFi reconnect ไม่ทำงาน | Blocking code ใน loop   | ตรวจสอบไม่มี `delay()`                      |

### 14.5 การตรวจสอบ mDNS/OTA โดยเฉพาะ

ก่อน Deploy ต้องมั่นใจว่า:

```bash
# 1. ค้นหาทุกที่ที่มีการประกาศ mDNS
grep -rn "MDNS.begin" src/ include/

# 2. ทุกที่ต้องใช้ OTA_HOSTNAME เหมือนกัน
grep -rn "OTA_HOSTNAME" src/ include/

# 3. ตรวจสอบค่าใน platformio.ini
grep "upload_port" platformio.ini
# ต้องตรงกับ OTA_HOSTNAME + ".local"
```
