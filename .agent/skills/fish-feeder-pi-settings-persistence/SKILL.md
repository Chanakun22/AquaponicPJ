---
name: fish-feeder-pi-settings-persistence
description: Use when debugging Fish Feeder settings not saving on the Pi dashboard, Feed Duration (ms) persistence, settings.json path/schema issues, or UI-to-API-to-MQTT key mapping for fish feeder config.
---

# Fish Feeder Pi Settings Persistence

This skill captures the current contract for Fish Feeder settings on the Pi dashboard. Use it when a feeder field looks like it saved but reloads to the old value, or when adding new feeder settings to the Pi stack.

## 1. Scope

- This skill is only for Fish Feeder settings and Pi-side persistence.
- It does **not** cover water-system refill logic, route decisions, or water safety behavior.

## 2. Canonical Field Contract

### 2.1 UI fields

- Feed duration input id: `feeder_duration_ms`
- Feed day input id: `feeder_day`
- Feed time input id: `feeder_time`
- Source input id: `feeder_command_source`
- Enable toggle id: `feeder_enabled`

### 2.2 Pi API payload keys

- `command_source`
- `enabled`
- `feed_day`
- `feed_time`
- `duration_ms`
- optional manual action flag: `trigger_feed`

### 2.3 Firmware / MQTT contract

- Local MQTT config topic: `aquaponics/config/fish_feeder`
- Local MQTT status topic: `aquaponics/status/fish_feeder`
- Duration key remains `duration_ms` end-to-end for the Pi/local MQTT path

## 3. Pi Persistence Rules

- Resolve `settings.json` from the same directory as `pi_server/app.py`, not from the process working directory.
- Merge loaded JSON with the default schema so old config files do not silently drop the `fish_feeder` section.
- Sanitize feeder numeric values during both load and save.
- Current accepted range for `duration_ms` is `250..10000` ms.
- After a successful save from the web UI, reload the settings from the server so the form shows the persisted value, not just the browser copy.

## 4. Save Paths To Check

If `Feed Duration (ms)` or another feeder value looks like it did not save, check these in order:

1. `settings.html` sends `duration_ms` in the request payload.
2. `app.py` route normalizes and stores `fish_feeder.duration_ms`.
3. Pi writes to the intended `pi_server/settings.json` file.
4. A later status sync does not overwrite the saved config with stale feeder state.
5. Reloading `/api/settings` returns the updated `fish_feeder` object.

## 5. Common Failure Modes

- Relative `SETTINGS_FILE` points to a different working directory, so save and load hit different files.
- Legacy `settings.json` lacks `fish_feeder`, and load logic returns the old schema without merging defaults.
- UI uses one key while API or MQTT expects another key.
- Browser state is updated optimistically, but the page never re-fetches the actual saved config.
- Status payload merges only runtime fields and masks a persistence problem.

## 6. Files Usually Involved

- `pi_server/settings.html`
- `pi_server/app.py`
- `pi_server/settings.json`
- `src/localMqtt.cpp`
- `src/fishFeeder.cpp`
- `include/fishFeeder.h`
- `CHANGELOG.md`

## 7. Verification Checklist

- Change `Feed Duration (ms)` from the Pi settings page.
- Save with both paths when relevant:
  - `Save General Settings`
  - `Apply Feeder Config`
- Refresh the page and confirm the same value is still shown.
- Confirm `/api/settings` returns the updated `fish_feeder.duration_ms`.
- Confirm MQTT publish for fish feeder still uses `duration_ms`.
- Confirm no unrelated runtime status field overwrote the stored feeder config.

## 8. Implementation Reminder

- Do not add a feeder setting in the UI unless all three layers exist:
  - UI field
  - Pi API save/load path
  - actual consumer in ESP32 or MQTT path
- Prefer fixing root-cause persistence bugs over hiding stale values with frontend-only state.

_Last Updated: 2026-04-26_