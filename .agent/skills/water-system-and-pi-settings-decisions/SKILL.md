---
name: water-system-and-pi-settings-decisions
description: Use when working on water system AUTO refill routing, fish-safe refill limits, mix-tank high safety stop behavior, Pi settings card layout, fish feeder settings persistence, or syncing defaults between firmware and Pi dashboard.
---

# Water System And Pi Settings Decisions

This skill captures the current project decisions that came out of the recent water-system and Pi-dashboard work. Use it before changing firmware refill logic, Pi settings surfaces, or fish feeder save behavior.

## 1. Water System Behavior

### 1.1 AUTO route semantics

- `AUTO` means **fish-first fallback**, not direct-sump-first.
- When fish route is available, prefer `FISH_TANK` first.
- Only fallback to `SUMP_DIRECT` when `allow_direct_sump_refill` is enabled **and** the fish route cannot continue or did not finish the refill need.

### 1.2 Fish-safe refill rule

- Fish refill must **not** be allowed to run until the mix tank is full by overflowing through the fish tank.
- A `FISH_TANK_REFILL` cycle is latched once it starts; AUTO mode must not cancel the fish route just because the mix-tank low sensor clears mid-cycle.
- A `FISH_TANK_REFILL` cycle must stop when any condition below is met:
  - `fish_refill_max_runtime_ms` limit is reached
  - fish overflow / over-level sensor becomes true
  - mix-tank high sensor becomes true as a safety stop
- After stopping the fish refill leg, the system must re-check whether the mix tank still needs water before deciding to fallback.

### 1.3 Status expectations

- `fish_refill_ready` and `fish_refill_wait_remaining_ms` must reflect the real cooldown/runtime state, not placeholder values.
- Water status exposed to Pi should explain both the active route and the reason the state machine stopped, waited, or alarmed.

## 2. Defaults That Must Stay In Sync

- `fish_refill_max_runtime_ms` default is **30000 ms** in firmware and Pi.
- Pi `settings.html` defaults and `app.py` fallback defaults must match firmware defaults.
- The value must remain editable in the Settings UI; do not hard-lock the field just because the default is safety-oriented.

## 3. Pi Settings UX Decisions

- Water-system settings should stay grouped in a single dedicated card when practical.
- That card should include:
  - circulation enable
  - auto-refill enable
  - refill timing policy
  - fish refill max time and cooldown
  - route selection and fallback toggle
  - live status / reason / alarm visibility
- If the card becomes too crowded, split into a dedicated water page rather than scattering settings across unrelated cards.

## 4. Fish Feeder Save Contract

### 4.1 Field mapping

- UI input id: `feeder_duration_ms`
- Pi payload key: `duration_ms`
- Local MQTT config key: `duration_ms`

### 4.2 Persistence rules on Pi

- Resolve `settings.json` relative to `pi_server/app.py`, not the process working directory.
- Merge loaded JSON with the default schema so legacy config files do not silently drop newer sections like `fish_feeder` or `water_system`.
- Sanitize/clamp feeder duration when loading and saving. Current allowed range is `250..10000` ms.
- After saving from the web UI, reload settings from the server so the page reflects the actual persisted value.

### 4.3 Save-path gotcha

- If `Feed Duration (ms)` looks like it did not save, check these points in order:
  - browser payload contains `duration_ms`
  - `/api/settings` or `/api/fish_feeder/config` normalizes and stores `fish_feeder.duration_ms`
  - Pi is writing the intended `pi_server/settings.json`, not a different relative-path file
  - a later status update is not overwriting the value with stale data

## 5. Files Usually Involved

- Firmware:
  - `include/config.h`
  - `include/waterSystem.h`
  - `src/waterSystem.cpp`
  - `src/localMqtt.cpp`
- Pi Server:
  - `pi_server/app.py`
  - `pi_server/settings.html`
  - `pi_server/pwa/sw.js`
- Flow / docs:
  - `forTestFlow/water-system-auto-flow.json`
  - `CHANGELOG.md`

## 6. Verification Checklist

- For water-system behavior changes:
  - confirm `AUTO` still goes fish-first
  - confirm fish refill stays latched until an explicit stop condition is met
  - confirm fish refill stops on timeout, overflow sensor, or mix-tank high sensor
  - confirm fallback only happens after re-evaluating remaining mix-tank need
- For Pi settings changes:
  - confirm load/save keys match between `settings.html`, `app.py`, and MQTT payloads
  - confirm reloading the page keeps the saved value
  - confirm defaults shown in the UI match backend defaults
- For documentation:
  - update `CHANGELOG.md` after significant changes

## 7. Working Style Reminder

- Do not claim a settings field exists unless the full path exists: UI input, API save/load, and consumer logic.
- Keep fish-safety decisions explicit in code comments, labels, or changelog text when behavior changes.
- Prefer fixing root-cause persistence issues over patching the UI to hide stale values.

_Last Updated: 2026-05-10_