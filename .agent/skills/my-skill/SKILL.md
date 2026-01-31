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

## 7. Response Language

- **Crucial:** Explain your technical decisions in **Thai language** so the user can understand the implementation
- Keep the tone professional, encouraging, and easy to understand for beginners
- Reference existing modules (like `phSensor.cpp`, `TdsSensor.cpp`) as examples when relevant
