---
name: debug-mantra
description: Four-mantra debugging discipline for ESP32/FreeRTOS firmware — reproduce, trace the fail path, falsify the hypothesis, cross-reference every breadcrumb. Apply whenever debugging starts.
---

# Debug Mantra

Four-step discipline for any debug session. Recite verbatim, then apply in order.

## Recite this — verbatim, as the first thing in your first response

> **Mantra:**
> 1. **First is reproducibility.** Can the issue be reproduced reliably?
> 2. **Know the fail path.** Debugger first; then source trace + knob enumeration; then in-code instrumentation.
> 3. **Question your hypothesis.** What would disprove it?
> 4. **Every run is a breadcrumb.** Cross-reference all of them.

Then begin work.

---

## 1. Reproduce reliably

Build a runnable repro before anything else.

- **Reliable repro** → capture the exact steps, inputs, and environment as a runnable artifact: failing test, CLI command, serial log sequence, specific sensor state.
- **Flaky repro** → the bug is not yet debuggable. Raise the rate first: loop the trigger, stress timing, narrow windows, inject delays. 50% flake is debuggable; 1% is not.
- **No repro at all** → stop. Say so explicitly. Ask for serial logs, crash dump (`crash` CLI command), task health output, or permission to instrument. Do **not** proceed to hypothesise.

Target: a fast, deterministic pass/fail signal.

### ESP32-specific repro tools
- `status` / `health` / `tasks` / `crash` CLI commands
- Serial monitor logs with timestamps
- Native unit tests (`pio test -e native`)
- Hardware test page on Pi dashboard
- NVS state inspection via `Preferences` dump

## 2. Know the fail path

Once reproducible, find *where* the code breaks. Try in this order — escalate only when the prior tactic fails.

1. **Serial/Telnet trace.** Check LOG_ERROR/WARN output. Use `crash` command for last task stage.
2. **Source trace + knob enumeration.** Trace the code path end-to-end and list every knob:
   - Config values (NVS, `config.h` constants)
   - Sensor state (valid/invalid, cached values)
   - FreeRTOS task state (which core, stack remaining, heartbeat)
   - Timing (millis intervals, watchdog timeouts)
   - MQTT connectivity state
   - Interlock conditions (waterSystem ↔ automator, command source)
3. **In-code instrumentation.** Add `LOG_DEBUG` at the suspected fail site with unique prefix (e.g. `[DBG-xxxx]`) so cleanup is a single grep.

### Common fail paths in this project
- WDT reset → blocking call without `systemTaskHeartbeat()`
- Stale sensor value → EMA not reset after calibration / invalid read streak
- Double-trigger → state not persisted across reboot (NVS)
- MQTT timeout → `WiFiClient.setTimeout()` missing or too high
- Stack overflow → task stack too small for JSON serialization

## 3. Falsify the hypothesis

When a candidate root cause surfaces, scrutinise it **before** testing it.

- Does it actually explain the symptom end-to-end? Walk it through.
- What is the simplest **proof**? What is the cleanest **disproof**?
- Run the **disproof first**. If the hypothesis survives, it's real.
- Generate 3–5 ranked hypotheses, not one. Single-hypothesis thinking anchors on the first plausible idea.

## 4. Every run is a breadcrumb

Maintain a running **ledger** of every experiment in this session.

Each entry: what changed, what happened, what it ruled in or out.

- When a new hypothesis surfaces, walk the ledger. Does it hold for **every** prior observation?
- If any past run contradicts it, the hypothesis is wrong or incomplete — refine or discard.
- When in doubt, design the **single experiment** whose outcome makes it certain. Run that next.

---

## Operating rules

- Recite the mantra block **once** per debug session, in your first response.
- Apply the four steps **in order**:
  - Do not propose a fix before #1 is satisfied (reliable repro exists).
  - Do not start testing hypotheses before #2 has narrowed the fail path.
  - Do not commit to a hypothesis before #3 has tried to disprove it.
  - Do not declare a hypothesis correct until #4 confirms it against every prior breadcrumb.
- If you catch yourself proposing a fix without a reliable repro, stop and return to step 1.
