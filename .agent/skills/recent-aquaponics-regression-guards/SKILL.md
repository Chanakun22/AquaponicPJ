---
name: recent-aquaponics-regression-guards
description: Use when editing TDS calibration or stability, water refill routing or safety, NETPIE rc=-2 cloud reconnect issues, flow docs, or README pin maps after recent aquaponics fixes.
---

# Recent Aquaponics Regression Guards

This skill packages the decisions and failure patterns from the recent TDS, water-system, NETPIE, flow-doc, and README cleanup work.

Use it before changing firmware behavior, Pi integration, or documentation in these areas.

## 1. TDS Calibration And Stability Rules

### 1.1 Current scale decision

- `include/config.h` currently uses `TDS_CONVERSION_FACTOR 0.695f`.
- This replaced the older DFRobot-style `0.5` factor because the real handheld reference was closer to `1413 uS/cm ~= 982 ppm`.
- Do not revert to `0.5` unless the whole measurement scale is revalidated against the real meter and standard solution being used.

### 1.2 Calibration safety guard

- `include/config.h` currently uses `TDS_MIN_CALIBRATION_SPAN_V 0.050f`.
- Two-point calibration must be rejected when the normalized voltage span is too small.
- Narrow-span calibration amplifies ADC noise and can make pump activity or bubbles look like chemistry changes.

### 1.3 Runtime behavior that should stay aligned

- TDS runtime and calibration are both temperature-aware.
- The live reading path already has filtering, deadband, and max-step limits to reduce small jitter.
- If TDS shifts while pumps run, treat code/model/calibration as a possible cause before blaming only turbulence or bubbles.

### 1.4 Files usually involved

- `include/config.h`
- `src/TdsSensor.cpp`
- `src/localMqtt.cpp`
- `pi_server/app.py`
- `pi_server/settings.html`
- `test/test_native/test_tds_native.cpp`

## 2. Water System Safety Rules

### 2.1 Fish-route refill is a latched cycle

- Once `FISH_TANK_REFILL` starts, the cycle must keep running until an explicit stop condition is met.
- AUTO mode must not cancel fish refill just because the mix-tank low sensor clears during the cycle.

### 2.2 Current stop conditions

- Fish refill stops when any of these becomes true:
  - `fish_refill_max_runtime_ms` limit reached
  - fish overflow sensor active
  - mix-tank high sensor active
- The mix-tank high sensor is a safety stop in both AUTO and manual fish-route behavior.

### 2.3 Automation interlock

- `automator` must stay blocked or paused while the mix tank is refilling or settling.
- If water-system status changes, confirm the Pi-facing status fields still explain why automation is blocked.

### 2.4 Flow-doc contract

- `forTestFlow/water-system-auto-flow.json` must match the real state-machine behavior in `src/waterSystem.cpp`.
- If refill route logic or stop conditions change, update the flow JSON and any related planner/demo content instead of leaving the diagram stale.

### 2.5 Files usually involved

- `include/config.h`
- `include/waterSystem.h`
- `src/waterSystem.cpp`
- `src/automator.cpp`
- `src/localMqtt.cpp`
- `pi_server/app.py`
- `pi_server/settings.html`
- `test/test_native/test_water_system_native.cpp`
- `forTestFlow/water-system-auto-flow.json`

## 3. NETPIE Versus Local MQTT Triage

### 3.1 Meaning of `rc=-2`

- Repeated `MQTT connection failed, rc=-2` in this project comes from `src/netpie.cpp`, not `src/localMqtt.cpp`.
- In this context it means TCP connect to the NETPIE broker failed.
- Treat it as cloud path unreachable first, not as a credentials error.

### 3.2 Current project behavior

- When Local MQTT is healthy, cloud reconnects must not degrade the Pi dashboard path.
- Firmware now pauses NETPIE retries for 30 minutes after 3 consecutive `rc=-2` failures while Local MQTT is still connected.
- If Local MQTT drops, NETPIE retry cooldown is cleared so the cloud path can be retried immediately.

### 3.3 What to check before changing code again

- Internet reachability from the Pi/AP path
- NAT/forwarding from the aquaponics LAN to the internet
- DNS resolution for `mqtt.netpie.io`
- outbound port `1883`

### 3.4 Files usually involved

- `include/config.h`
- `src/netpie.cpp`
- `src/main.cpp`
- Pi network or AP setup docs/scripts when the issue is environmental

## 4. Documentation And Pin-Map Guards

### 4.1 Input versus output classification

- `SUMP_LEVEL_LOW_PIN`, `SUMP_LEVEL_HIGH_PIN`, and `FISH_TANK_OVERFLOW_PIN` are inputs.
- Do not list those sensors under output/actuator tables.
- Output tables should contain only controllable actuators such as pumps, relays, valves, feeder, and fan.

### 4.2 Documentation trust rule

- Do not trust older README text blindly.
- Re-check pin maps, routes, page names, and current modules against the actual code before documenting them.
- If a doc summary disagrees with `include/config.h`, `src/*.cpp`, or `pi_server/header.js`, update the doc instead of carrying the mismatch forward.

## 5. Verification Checklist

- For TDS changes:
  - run `pio test -e native -f test_tds_native`
  - run `pio run -e production`
- For water-system changes:
  - run `pio test -e native -f test_water_system_native`
  - run `pio run -e production`
- For NETPIE reconnect changes:
  - run `pio run -e production`
  - verify Local MQTT behavior still stays responsive while cloud is unavailable
- For flow-doc or README changes:
  - confirm pin classification and route semantics against code
  - update `CHANGELOG.md` after significant edits

## 6. Working Style Reminder

- Fix root-cause behavior before smoothing or hiding symptoms.
- Prefer the owning code path over broad repo exploration.
- When documentation changes, re-check the physical meaning of every pin and sensor instead of copying grouped lists mechanically.
- If one skill or doc already contains overlapping guidance, update it so future instructions do not conflict.

_Last Updated: 2026-05-10_