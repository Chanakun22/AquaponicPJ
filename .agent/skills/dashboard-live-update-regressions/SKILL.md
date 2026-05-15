---
name: dashboard-live-update-regressions
description: Use when the Pi dashboard or hardware test page stops updating, TDS or pH stays at 0 or --, MQTT sensor packets seem stale, or live sensor cards regress after firmware/Pi changes.
---

# Dashboard Live Update Regressions

This skill captures the recurring failure modes that caused the main dashboard and HW test pages to stop showing fresh values, especially `tds` and `ph`.

Use this before changing firmware MQTT payloads, Pi dashboard merge logic, or PWA/static dashboard files.

## 1. Known Recurrent Symptoms

- Main dashboard shows `ESP32 ONLINE` but sensor cards stay at `0`, `0.00`, or `--`.
- `tds` and `ph` disappear first while system health fields like uptime/RSSI still look alive.
- HW test water card stops reflecting `SUMP_LEVEL_LOW`, `SUMP_LEVEL_HIGH`, or `FISH_TANK_OVERFLOW` changes.
- After a frontend-only deploy, browser still serves an old dashboard or HW test page.
- Pi reconnects to MQTT but live cards still appear frozen until a manual refresh.

## 2. Primary Root Causes

### 2.1 Bloated `aquaponics/sensors` payload

- The main topic `aquaponics/sensors` must stay relatively slim.
- Regressions happened when too many water-system fields were mirrored into the main sensor payload.
- When the JSON approaches the PubSubClient buffer limit, Pi may still look online while fresh sensor values never arrive.
- Water-system detail belongs on the dedicated topic `aquaponics/status/water_system`.

### 2.2 Pi blanking values when keys are temporarily absent

- Firmware may omit a sensor key in a packet when a fresh read is temporarily unavailable.
- Pi must not immediately set `last_data["tds"]` or `last_data["ph"]` to `None` just because the key is missing from one MQTT payload.
- Only clear a sensor value when that sensor is explicitly disabled by `sensor_config`.

### 2.3 Delayed WebSocket propagation on Pi

- Relying only on the background SocketIO broadcast loop can make the dashboard appear stale.
- After Pi receives fresh sensor MQTT data, it should emit `dashboard_update` immediately.
- A REST fallback such as `/api/dashboard_snapshot` is useful when WebSocket delivery or client state lags.

### 2.4 PWA cache serving stale UI

- When `index.html`, `hardware_test.html`, shared JS, or static dashboard files change, the browser may keep old content.
- Bump `pi_server/pwa/sw.js` `CACHE_NAME` whenever static dashboard behavior changes.

### 2.5 Partial deploys

- Some regressions were caused by updating only Pi or only firmware.
- Dashboard live-data fixes often require both:
  - firmware MQTT payload update
  - Pi backend merge/update logic

## 3. Current Safe Design Rules

### 3.1 Topic split

- `aquaponics/sensors` is for main sensor values and compact system/dashboard fields.
- `aquaponics/status/water_system` is for detailed water state.
- Do not mirror large water-state structures back into `aquaponics/sensors` unless packet size is re-measured and validated.

### 3.2 Buffer sizing and guardrails

- If `src/localMqtt.cpp` adds new sensor payload fields, review all of these together:
  - `LOCAL_MQTT_PACKET_BUFFER_SIZE`
  - `_sensorPublishDoc` capacity
  - overflow checks before serialize/publish
- If water status grows, separately review `_publishWaterSystemStatus()` buffer/doc size.

### 3.3 Pi merge contract

- `pi_server/app.py` should preserve the last good sensor reading across transient missing keys.
- `build_dashboard_data()` must keep returning `sensors`, `health`, `info`, and `logs` in the structure `index.html` expects.
- If water status is merged into dashboard payloads, dedicated water status should beat stale fallback data.

### 3.4 Deployment contract

- For live-update bug fixes, expect to deploy at least:
  - firmware from `src/localMqtt.cpp` and any dependent firmware changes
  - `pi_server/app.py`
  - `pi_server/index.html` or `pi_server/hardware_test.html` when frontend behavior changed
  - `pi_server/pwa/sw.js` if static files changed

## 4. Files Usually Involved

- Firmware:
  - `include/config.h`
  - `src/main.cpp`
  - `src/localMqtt.cpp`
  - `src/waterSystem.cpp`
- Pi backend:
  - `pi_server/app.py`
- Pi frontend:
  - `pi_server/index.html`
  - `pi_server/hardware_test.html`
  - `pi_server/pwa/sw.js`

## 5. Fast Triage Order

Follow this order instead of jumping between UI and firmware randomly.

1. Check whether Pi is receiving fresh `aquaponics/sensors` data at all.
2. If dashboard shows online but sensor cards stay `0/--`, inspect `src/localMqtt.cpp` payload size first.
3. If Pi receives packets but values flicker to `--`, inspect `pi_server/app.py` merge/blanking logic.
4. If backend looks correct but browser still shows old behavior, bump `pi_server/pwa/sw.js` cache version.
5. If a fix touched both firmware payload and Pi merge logic, deploy both sides before judging the result.

## 6. Regression Checks Before Merging

- Firmware:
  - `pio run -e production` passes
  - no sensor payload overflow warning path is introduced silently
  - `tds` and `ph` still appear in `localMqttPublishData()` under normal conditions
- Pi backend:
  - `dashboard_update` still carries `data.sensors.tds` and `data.sensors.ph`
  - missing sensor keys do not wipe the last good reading unless disabled in `sensor_config`
  - MQTT reconnect logic does not leave the app stuck after broker restart
- Pi frontend:
  - main dashboard updates from `dashboard_update`
  - any polling fallback still points to a valid endpoint if used
  - `sw.js` cache version changes when static behavior changes

## 7. Known Good Reference

- Commit `914f7d37fc718c54e6c28aae0ba07f26d90bf095` is a useful reference point for the dashboard/HW-test path when comparing regressions.
- If behavior regresses again, diff these files first against that commit:
  - `src/localMqtt.cpp`
  - `pi_server/app.py`
  - `pi_server/index.html`
  - `pi_server/hardware_test.html`
  - `pi_server/pwa/sw.js`

## 8. Working Style Reminder

- Fix the transport/merge root cause before changing UI formatting.
- Treat `ESP online but cards at 0/--` as a data-path bug first, not a presentation bug.
- Do not re-bloat `aquaponics/sensors` just to make one card update unless packet headroom is proven.
- Update `CHANGELOG.md` after significant live-update fixes.

_Last Updated: 2026-05-02_