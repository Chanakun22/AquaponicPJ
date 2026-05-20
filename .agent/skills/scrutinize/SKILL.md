---
name: scrutinize
description: Outsider-perspective end-to-end review of a plan, PR, or code change. Questions intent, traces actual code paths, and verifies the change does what it claims.
---

# Scrutinize

Stand outside the change and ask whether it should exist at all, then verify it actually does what it claims end-to-end.

## Workflow — run in order, do not skip ahead

### 1. Intent — what is this actually trying to do?

- State the goal in one sentence, in your own words. If you cannot, the artifact is underspecified — say so and stop.
- Ask: **is there a simpler, smaller, or more elegant way to achieve the same goal?** Consider:
  - Doing nothing (is the problem real / load-bearing?).
  - Using something that already exists in the codebase.
  - A smaller change that solves 90% of the goal with 10% of the risk.
  - Solving it at a different layer (config.h vs code, Pi vs firmware, build vs runtime).

### 2. Trace — walk the actual code path

- For each behavior the change claims, trace the path end-to-end through the real code:
  - Entry point → call sites → branches taken → state mutated → exit / side effect.
- Include the unchanged code on either side of the diff. Bugs hide at the seams.
- Note every place the trace surprises you. Surprises are signal.

#### Project-specific trace points
- FreeRTOS task boundaries (TaskSensors → cached value → TaskControl → TaskNetworking)
- NVS read/write pairs (open → get/put → end)
- MQTT publish/subscribe key matching (ESP32 ↔ Pi)
- Interlock conditions (waterSystem blocking automator, command source gating)
- Active-low relay logic (`PUMP_ON`/`PUMP_OFF`)

### 3. Verify — does it actually do what it claims?

For each claim the change makes, answer:
- **Does the code path you traced actually produce that behavior?**
- **What inputs / states would break it?** Edge cases: NaN sensor, NVS corruption, WiFi disconnect mid-publish, reboot during active state, millis() overflow.
- **What does it silently change?** Timing, memory usage, MQTT payload size, NVS wear.
- **How is it tested?** Do native tests exercise the traced path?

### 4. Report

One section per finding. Order by severity (blocker → major → nit).

For each:
- **Finding** — one sentence, specific. Cite `file:line`.
- **Why it matters** — the consequence.
- **Evidence** — the trace step or input that exposes it.
- **Suggested change** — concrete, minimal.

Close with verdict: **ship / fix-then-ship / rework / reject** — with the single biggest reason.

## Operating rules

- No rubber-stamps. If you find nothing, say what you traced and checked.
- Cite or it didn't happen. Every claim references a specific path, file, or line.
- One simpler-alternative pass is mandatory.
- Don't pad with style nits when there's a structural problem.
- No flattery, no hedging. State the finding.
