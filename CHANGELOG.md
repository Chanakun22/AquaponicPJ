# Changelog

All notable changes to the **Smart Aquaponics AI** project will be documented in this file.

## [2026-04-13] - Future-Ready Water System Scaffold

### Added

- **feat: water system scaffold (`waterSystem.cpp`, `waterSystem.h`):**
  - เพิ่มโมดูลคุมปั๊มน้ำวนหลักและปั๊มเติมน้ำแบบ future-ready
  - รองรับสถานะ `DISABLED`, `CIRCULATION`, `REFILLING`, `BLOCKED`, `ALARM`
  - รองรับ logic refill timeout, overflow alarm, และ sensor contradiction
  - ใช้ค่า default แบบปลอดภัยเมื่อยังไม่กำหนด GPIO/เซ็นเซอร์จริง
- **feat: CLI water commands (`commandHandler.cpp`):**
  - เพิ่มคำสั่ง `water`, `circ on`, `circ off`, `refill on`, `refill off`, `water clear`
- **feat: local MQTT water status/config (`localMqtt.cpp`, `config.h`):**
  - เพิ่ม topic `aquaponics/config/water_system` และ `aquaponics/status/water_system`
  - แนบสถานะระบบน้ำเข้า payload `aquaponics/sensors`
- **docs: hardware shopping list (`HARDWARE_TO_BUY.md`):**
  - สรุปรายการฮาร์ดแวร์ที่ควรซื้อเพิ่มจากสถานะปัจจุบันของระบบ

### Changed

- **fix: water alarm interlock (`automator.cpp`):**
  - ให้ automator หยุดจ่ายปุ๋ยเมื่อระบบน้ำมี alarm หรือเมื่อปั๊มน้ำวนหลักไม่ทำงาน
- **feat: Pi water system integration (`pi_server/app.py`, `pi_server/settings.html`):**
  - เพิ่ม API และหน้า settings สำหรับตั้งค่า circulation, refill, manual refill, และ clear alarm
  - เพิ่มการ sync สถานะ water system จาก MQTT เข้า Pi memory โดยไม่เขียนไฟล์ถี่เกินจำเป็น
- **feat: periodic water status publish (`localMqtt.cpp`):**
  - ส่ง `aquaponics/status/water_system` เป็นระยะเมื่อ local MQTT เชื่อมต่อ เพื่อให้ dashboard/backend เห็นสถานะล่าสุดต่อเนื่อง

## [2026-04-09] - Hardware Pin Rework & WiFi Direct Connect (v2.5.0)

### Changed

- **refactor: WiFi Direct Connect (`wifiConn.cpp`):**
  - เปลี่ยนจาก WiFiManager (AP Portal) → `WiFi.begin()` ต่อตรงไปยัง Pi Hotspot
  - SSID: `Aquaponics-LAN`, Password: `aqua1234` (จาก `hostapd.conf`)
  - ลบ dependency `WiFiManager` library ออก — ลด Flash usage
  - Auto-reconnect ทุก 10 วินาที (เดิม 30 วินาที)
- **refactor: Light Controller → Relay (`lightController.cpp`):**
  - เปลี่ยนจาก NeoPixel RGB LED (GPIO 48) → Relay ผ่าน `LIGHT_RELAY_PIN` (GPIO 12)
  - ลบ dependency `Adafruit_NeoPixel` library ออก
  - ใช้ `digitalWrite()` แบบ Active LOW (LOW = ON, HIGH = OFF)
- **refactor: Pin Assignment (`config.h`):**
  - `LIGHT_RELAY_PIN` เปลี่ยนจาก `LED_BUILTIN` → GPIO 12
  - ลบ `PUMP_WATER_PIN` ออกจาก codebase ทั้งหมด (ยังไม่ใช้)
  - ลบ references จาก: `localMqtt.cpp`, `commandHandler.cpp`, `automator.cpp`

### Removed

- **`PUMP_WATER_PIN`** — ลบ define + ลบ `pump_w` CLI command + ลบ hw_test handler
- **`Adafruit_NeoPixel`** — ไม่ใช้ NeoPixel แล้ว (ใช้ Relay แทน)
- **`WiFiManager`** — ไม่ใช้ AP Portal แล้ว (ต่อตรง Pi)

## [2026-03-26] - Offline-Safe Networking

### Fixed

- **fix: NETPIE MQTT reconnect spam (`netpie.cpp`):**
  - เพิ่ม Exponential Backoff (5s → 10s → 20s → 30s → 60s max) แทน fixed 5s interval
  - ลด log spam จาก ~12 ครั้ง/นาที เหลือ ~1-2 ครั้ง/นาที เมื่อ broker unavailable
  - Reset backoff กลับ 5s เมื่อเชื่อมต่อสำเร็จ
- **fix: Skip network services when WiFi offline (`main.cpp`):**
  - `telnetLoop()` และ `otaLoop()` จะไม่ถูกเรียกเมื่อ WiFi ยังไม่เชื่อม
  - ลด CPU waste จาก socket operations ที่ทำไม่ได้
- **fix: getLocalTime() blocking (`lightController.cpp`):**
  - เพิ่ม timeout 10ms แทน default 5s — ป้องกัน block เมื่อ NTP ยังไม่ sync
- **fix: Log queue overflow เมื่อ MQTT offline (`localMqtt.cpp`):**
  - ไม่ queue log เมื่อ MQTT ยังไม่เชื่อม — ประหยัด memory

## [2026-03-25] - Header Navigation & Responsive Fix

### Changed

- **feat: Mobile Hamburger Menu (`header.js`):**
  - เพิ่มปุ่ม ☰ hamburger สำหรับมือถือ — กดแล้ว nav เปิดแนวตั้ง (เปลี่ยนเป็น ✕ ปิด)
  - Nav bar เปลี่ยนจาก horizontal scroll เป็น vertical collapse บน mobile (≤768px)
  - ซ่อน Net Stats pills บนหน้าจอเล็กมาก (≤480px) เพื่อประหยัดพื้นที่
  - Nav links มี touch target ใหญ่ขึ้น (12px padding, full-width) สำหรับมือถือ
- **refactor: ลบ CSS ซ้ำ (`graphs.html`, `terminal.html`, `ota.html`):**
  - ลบ nav-bar/nav-link CSS overrides ที่ซ้ำกับ `header.js` ออกจาก 3 หน้า
  - Nav styling ทั้งหมดจัดการจาก `header.js` ที่เดียว (Single Source of Truth)

## [2026-03-25] - Fix Networking Task Stuck (30s Timeout)

### Fixed

- **fix: MQTT Connect Blocking (`netpie.cpp`, `localMqtt.cpp`):**
  - ตั้ง `WiFiClient.setTimeout(5)` ลด TCP socket timeout จาก ~30 วินาที → 5 วินาที
  - ตั้ง `PubSubClient.setSocketTimeout(5)` ลด MQTT keepalive timeout
  - เพิ่ม heartbeat + WDT reset ระหว่าง `netpieLoop()` กับ `localMqttLoop()` ใน `TaskNetworking` (main.cpp)
  - แก้ปัญหา "STUCK TASK: Networking — no heartbeat for 30 seconds" เมื่อ NETPIE หรือ Pi unreachable

## [2026-03-24] - Web Dashboard Performance Optimization

### Changed

- **perf: Dashboard WebSocket Migration (`index.html`):**
  - เปลี่ยนจาก HTTP Polling (5 API calls ทุก 3 วินาที) → WebSocket (SocketIO) ที่ server push ข้อมูลมาให้ทุก 2 วินาที
  - ลดจำนวน HTTP requests จาก ~100/นาที เหลือ ~2/นาที (settings fetch ทุก 30 วินาที)
  - ข้อมูลเซนเซอร์อัปเดตเร็วขึ้นและลดภาระ CPU ของ Pi อย่างมาก
- **perf: Net Stats Polling Interval (`header.js`):**
  - ลด polling `/api/wifi/netstats` จากทุก 3 วินาที → ทุก 10 วินาที ลดการรัน `ping` command บน Pi
- **perf: Chart.js Local Hosting (`graphs.html`):**
  - ย้าย Chart.js, HammerJS, chartjs-plugin-zoom จาก CDN ภายนอกมาเป็นไฟล์ local (`/static/js/`)
  - กราฟทำงานได้แม้ไม่มีอินเทอร์เน็ต (Offline Mode)
- **refactor: ลบ CSS ซ้ำใน `index.html`:**
  - ลบ Nav Bar CSS ~40 บรรทัดที่ซ้ำกับ `header.js` และ `base.css`
  - เพิ่ม `will-change: transform` ให้ card animations เพื่อลดภาระ GPU
- **chore: PWA Cache Update (`sw.js`):**
  - Bump cache version `v2` → `v3`
  - เพิ่ม Socket.IO, Chart.js, HammerJS, chartjs-plugin-zoom เข้า PWA cache สำหรับ offline
- **chore: Updated `download_assets.sh`:**
  - เพิ่มคำสั่งดาวน์โหลด Chart.js + plugins สำหรับ offline mode

## [2026-03-20] - Login System & Admin Panel

### Added

- **Authentication System (`app.py`):**
  - Flask session-based login with `login_required` decorator on all protected routes.
  - Hashed passwords via `werkzeug.security` stored in `auth_config.json` (gitignored).
  - API: `POST /api/login`, `POST /api/logout` with session management (7-day expiry).
  - Auto-redirect unauthenticated users to `/login` page.
- **Activity Logging (`app.py` + `admin_logs.html`):**
  - `activity_logs` SQLite table for admin audit trail.
  - Tracks: Login, Logout, Settings changes, TDS/pH Calibration, OTA uploads, User management.
  - Admin Logs page with filtering by action type and date.
- **User Management (`admin_users.html`):**
  - Admin page to add, edit password, and delete users.
  - API: `GET/POST /api/admin/users`, `POST /api/admin/users/password`, `POST /api/admin/users/delete`.
  - Cannot delete the `admin` account.
- **Login Page (`login.html`):**
  - Standalone dark-theme login page with glassmorphism design.
  - Animated login icon, error handling, AJAX form submission.
- **Navigation Update (`header.js`):**
  - Added "Activity" and "Users" nav links.
  - Added Logout button in header (red accent, all pages).

### Changed

- `app.py`: All 30+ routes now require authentication except `/login`, static assets, and PWA files.
- `.gitignore`: Added `pi_server/auth_config.json`.

## [2026-03-10] - Deployment Automation & Health Monitoring

### Added

- **Deployment automation scripts:**
  - `setup.sh`: Automated install script for Pi (packages, python libs, services).
  - `aquaponics.service`: Systemd service for the main dashboard app.
  - Improved `download_assets.sh` with relative path support.
- **System Health Enhancement:**
  - Implemented real sensor health check in `system.cpp`.
  - `SystemHealth_t::sensorsOk` now accurately reflects the status of all enabled sensors.
- **Unit Testing:**
  - Created `test/test_ph/test_ph_filter.cpp` to verify pH EMA filter and error handling (Native environment).

## [2026-03-02] - pH Sensor Filter Enhancement

### Changed

- **feat: pH EMA Filter (`phSensor.cpp`):**
  - เพิ่ม Exponential Moving Average (α=0.15) หลัง Trimmed Mean เพื่อ smooth ค่า pH ข้ามรอบ
  - Pipeline ใหม่: `ADC Buffer → Trimmed Mean (ตัด 20%) → EMA (α=0.15)`
  - รองรับ NaN จาก hardware validation (sensor หลุด/short circuit)

## [2026-02-16] - Pi AP+Client Bridge Mode

### Added

- **Pi AP Bridge (`pi_server/setup_ap.sh`):**
  - Pi Zero 2 W ทำงาน AP+Client พร้อมกัน: `wlan0` (Home WiFi) + `ap0` (Aquaponics-LAN)
  - ESP32 เชื่อม Pi ได้ตลอดแม้ Router พังหรือเปลี่ยน WiFi
  - `hostapd.conf`: AP config (SSID: Aquaponics-LAN, WPA2)
  - `dnsmasq_ap.conf`: DHCP server (192.168.10.x)
  - `setup_ap.sh`: Script อัตโนมัติ (ติดตั้ง + config + systemd + NAT)
- **ESP32 MQTT Fallback IP (`localMqtt.cpp`):**
  - เพิ่ม Static IP fallback (`192.168.10.1`) เมื่อ mDNS หาไม่เจอ
  - เพิ่ม `LOCAL_MQTT_STATIC_IP` ใน `config.h`

## [2026-02-11] - Bug Fixes (mDNS Blocking & SQLite Threading)

### Fixed

- **fix: mDNS Blocking (`localMqtt.cpp`):**
  - ลด mDNS query timeout จาก 1000ms → 200ms เพื่อลด blocking time เมื่อ Pi offline
  - เพิ่ม Exponential Backoff (5s → 10s → 20s → 40s → 60s) สำหรับ reconnect interval เมื่อ Pi หาไม่เจอ ช่วยลดภาระของ Networking Task
- **fix: SQLite Threading (`pi_server/app.py`):**
  - เพิ่ม `threading.Lock()` (`db_lock`) ครอบทุกจุดที่เข้าถึง SQLite (`init_db`, `save_data_to_db`, `save_settings_to_db`, `get_history`) เพื่อป้องกัน `database is locked` error จากการเขียนพร้อมกันของ MQTT Thread กับ Web Thread

## [2026-02-09-3] - Sensor Toggle & Dashboard Enhancements

### Added

- **Sensor Toggle Feature (ESP32 + Pi):**
  - Added ability to enable/disable individual sensors (TDS, pH, Water Temp, Air Temp, Light) via Web Dashboard.
  - Implemented `SensorId_t` enum and `systemSetSensorEnabled()`/`systemGetSensorEnabled()` in `system.cpp`.
  - Sensor states persisted in NVS (survives reboot).
  - MQTT topic `aquaponics/config/sensors` for receiving toggle commands from Pi.
  - MQTT topic `aquaponics/status/sensors` for state feedback from ESP32 to Pi.
- **Dashboard Disabled Indicator:**
  - Sensor cards on Dashboard now show "DISABLED" badge (red) when sensor is OFF.
  - Cards appear grayed out (grayscale + opacity) for visual clarity.

### Fixed

- **TDS Calibration UI:** Fixed invisible text in number inputs caused by CSS color issue. Redesigned layout to 2-column grid.
- **State Synchronization:** ESP32 now publishes its sensor config on MQTT connect and after updates, ensuring Pi settings stay in sync.

### Changed

- `settings.html`: Added "Sensor Management" section with toggle switches.
- `app.py`: Added `/api/settings` POST endpoint and MQTT subscription to `aquaponics/status/sensors`.
- `main.cpp`: `TaskSensors` now checks `systemGetSensorEnabled()` before reading each sensor.

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
