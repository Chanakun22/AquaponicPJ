# Changelog

All notable changes to the **Smart Aquaponics AI** project will be documented in this file.

## [Unreleased]

- Deployment automation scripts (`setup.sh`, `aquaponics.service`).

## [2026-02-09-2] - Security Hardening & Stability

### Fixed

- **Watchdog Timer Reset:**
  - Added `esp_task_wdt_reset()` to `loop()` in `main.cpp` to prevent 60-second system restarts.
- **Secrets Management:**
  - Moved all hardcoded secrets from `secrets.h` to `secrets.ini`.
  - Configured `platformio.ini` to inject secrets as build flags using safe stringification macros.
  - Fixed quoting issue in `secrets.ini` that caused MQTT auth failures.

### Changed

- **Code Refactoring:**
  - Refactored `telnetServer.cpp` and `commandHandler.cpp` to use C-style strings (`char*`) instead of `String` objects to prevent memory fragmentation.
  - Implemented FreeRTOS Tasks (`TaskNetworking`, `TaskSensors`, `TaskControl`) for better concurrency.
- **Verification:**
  - Verified codebase against `esp32-aquaponics-engineering-standard` (Passed).

### Added

- **Documentation:**
  - Created `walkthrough.md` summarizing the system hardening process.

## [2026-02-09] - TDS Advanced Calibration

### Added

- **TDS 2-Point Calibration:**
  - Implemented Linear Interpolation logic in `TdsSensor.cpp`.
  - Added Temperature Compensation with manual input in `settings.html`.
  - Seprate temperature inputs for Low and High calibration points for maximum accuracy.
  - Persisted calibration data in NVS (ESP32) and `settings.json` (Pi).
  - **TDS Stabilization:** Added weighted Moving Average Filter (Alpha 0.1) for smooth, stable readings.
  - **Advanced Math:** Replaced linear model with Hybrid Polynomial Calibration (Standard Curve + 2-Point Scaling).
  - **Precision Temp Comp:** Updated temperature coefficient to 0.019 (KCl Standard) for better accuracy.

### Changed

- **Code Refactoring:**
  - Optimized buffer logic in `TdsSensor.cpp` to remove unused variables.
  - Updated `config.h` to use standard intervals.
  - Verified code against Engineering Standards (PASS).

## [2026-02-08-2] - WiFi Reconnection Fixes

### Fixed

- **WiFi Auto-Reconnect (`wifiConn.cpp`):**
  - เพิ่ม `WiFi.setAutoReconnect(true)` เมื่อเชื่อมต่อสำเร็จ
  - เพิ่ม logic พยายาม `WiFi.reconnect()` ทุก 30 วินาทีเมื่อ WiFi หลุด
  - แก้ปัญหา ESP32 ไม่เชื่อมต่อ WiFi หลัง router restart
- **Local MQTT mDNS Re-resolution (`localMqtt.cpp`):**
  - เพิ่มการ re-resolve mDNS hostname หลัง connection ล้มเหลว 3 ครั้ง
  - แก้ปัญหา Pi IP เปลี่ยนหลัง router restart

## [2026-02-08] - Live Cam & PWA

### Added

- **Live Camera (720p@15fps):**
  - Added `live.html` for viewing MJPEG stream.
  - Created `start_cam.sh` for controlling `libcamera-vid`.
  - Integrated camera stream into Dashboard UI.
- **Progressive Web App (PWA):**
  - Added `manifest.json` and `sw.js` for "Add to Home Screen" capability.
  - Implemented offline caching for static assets.
  - Generated application icons.
- **Settings Integration:**
  - `start_cam.sh` script for easy camera startup.
- **Auto-Start System:**
  - Added camera auto-start logic to `app.py`.
  - Updated `setup.sh` to handle Python environment restrictions.

### Changed

- **Dashboard UI:**
  - Added "Live Cam" button to the header.
  - Updated `index.html` to register Service Worker.
- **Backend:**
  - Updated `app.py` to serve PWA assets and Live page.

## [2026-02-07] - Core Systems & IoT

### Added

- **Light Control System:**
  - Implemented `relay_module.cpp` on ESP32.
  - Added scheduler logic synced with NTP.
  - Integrated manual override via MQTT.
- **NETPIE Integration:**
  - Bi-directional shadow syncing for light status.
  - Fixed partial update bug in `netpie.cpp`.

### Changed

- **Codebase Optimization:**
  - Removed blocking `delay()` calls in `main.cpp`.
  - Refactored `wifi_manager.cpp` for stability.
  - Audited code against Engineering Standards (PASS).

## [2026-02-06] - Advanced Dashboard & Logs

### Added

- **Full Logs Viewer:**
  - Created `full_logs.html` with filtering and pagination.
  - Added API endpoint `/get_logs` in `app.py`.
- **Database:**
  - Implemented `aquaponics.db` (SQLite) for persistent sensor data.
- **DHT Sensor:**
  - Integrated DHT22 for air temperature/humidity.

## [2026-02-04] - Initial Web Interface

### Added

- **Basic Dashboard:**
  - `index.html` demonstrating real-time gauge updates.
  - `app.py` Flask server foundation.
- **MQTT Bridge:**
  - Setup local Mosquitto broker on Raspberry Pi.
  - Python script to bridge ESP32 data to Web UI.

## [2026-01-31] - Firmware Foundations

### Added

- **ESP32 Modules:**
  - Standardized non-blocking sensor modules.
  - `esp32-aquaponics-module` skill definition.
  - Initial `platformio.ini` configuration.

---

_Created by Antigravity AI_
