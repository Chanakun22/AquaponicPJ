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
