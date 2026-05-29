---
name: aquaponics-project-workflows
description: Guides Smart Aquaponics ESP32/PlatformIO firmware, MCP23017 migration, Pi dashboard, and verification workflows. Use when the user mentions MCP23017, gpioOut, relay output migration, PlatformIO build/upload/monitor, ESP32 firmware changes, sensors, MQTT, Pi dashboard, Web UI, or project changelog.
---

# Smart Aquaponics Project Workflows

## Core Rules

- Follow `AGENTS.md` first; this skill adds task-specific workflow reminders.
- Keep firmware non-blocking: no `delay()` or blocking loops in FreeRTOS tasks.
- Before adding constants or hardcoded values, search `include/config.h` and the codebase.
- Preserve shipped behavior for stable interfaces: MQTT keys, NVS data, CLI commands, and Pi APIs.
- Do not create or overwrite gitignored secrets such as `secrets.ini`, `include/secrets.h`, `pi_server/app.py`, or `pi_server/index.html` without explicit user approval.
- Update `CHANGELOG.md` after significant features, fixes, refactors, or project workflow additions.

## PlatformIO Workflow

- Build firmware with `pio run`.
- Upload over USB with `pio run --target upload`.
- Upload OTA with `pio run -e ota_upload -t upload`.
- Open serial monitor with `pio device monitor --baud 115200`.
- When a command fails, report the first actionable compiler/runtime error and the file/symbol involved.

## MCP23017 Test And Migration

Use this when testing CJMCU-2317 / MCP23017 relay output migration.

1. Verify wiring before power:
   - MCP `VCC` = `3.3V` only, not `5V`.
   - `GND` shared with ESP32.
   - `SDA` -> GPIO `8`, `SCL` -> GPIO `9`.
   - `RESET` -> GPIO `4`, with 10k pull-up to `3.3V`.
   - `A0/A1/A2` -> GND, so address is `0x20`.
2. Run serial CLI command `test`; I2C scan should show MCP at `0x20` and BH1750 at `0x23`.
3. Migrate one output at a time by changing one `OUT_USE_MCP_*` flag in `include/config.h` from `0` to `1`.
4. Use this migration order: Light, Fan, Fish Feeder, Route Valve, Circulation, Refill, Pump Nutrient A, Pump Nutrient B.
5. After each flag change: build, upload, monitor boot logs, test the relevant CLI command, then soak briefly.
6. If behavior is wrong, rollback that output by setting its `OUT_USE_MCP_*` flag back to `0` and rebuilding.

## Firmware Change Checklist

- Use existing module patterns: `src/module.cpp` plus `include/module.h`.
- Keep sensor reads cached and instant-returning; hardware polling belongs in `moduleLoop()`.
- Ensure MQTT clients use short timeouts and networking task heartbeats after potentially blocking calls.
- Match JSON keys on both ESP32 publishers and Pi consumers.
- Prefer focused native tests when behavior changes shared logic, calibration, state machines, or safety interlocks.

## Pi Dashboard Checklist

- Every page must use `base.css`, `header.js`, and the shared `.header > .header-top` structure.
- Do not duplicate nav CSS in pages; `header.js` is the single source of truth for navigation.
- Dashboard live data should use SocketIO/WebSocket, not HTTP polling.
- Use local JS libraries under `/static/js/`; do not add CDN dependencies.
- When static or PWA files change, bump `CACHE_NAME` in `pi_server/pwa/sw.js`.
- Do not add UI settings unless the backend API and consumer code actually use them.

## Verification

- After edits, run lints for changed files when available.
- For firmware changes, run `pio run`.
- For web/dashboard changes, verify relevant files and avoid breaking offline/local asset rules.
- Record meaningful completed work in `CHANGELOG.md` using the existing date-section style.
