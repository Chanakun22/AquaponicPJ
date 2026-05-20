---
name: post-mortem
description: Write the canonical engineering record of a fixed bug — root cause, mechanism, fix, validation, and how it slipped through. Use after a debug session lands a fix.
---

# Post-mortem

The canonical engineering record of a bug fix. Written **after** debugging lands a real fix, **for** future maintainers of this aquaponics system.

## Required inputs — refuse to draft without these

- [ ] **Reliable repro** exists (deterministic or high-rate-flake)
- [ ] **Root cause is known** (mechanism identified, not hypothesis)
- [ ] **Fix is identified** (commit / file changes)
- [ ] **Fix is validated** (build passes, test passes, or hardware verified)

If any are missing, list what's missing and stop.

## Structure

### 1. Summary _(mandatory)_

One paragraph. What broke, in user/system terms. What fixed it, in one sentence.

### 2. Symptom

What was actually observed. Serial log, sensor reading, dashboard state, CLI output. Concrete — don't paraphrase.

### 3. Root cause _(mandatory)_

The actual bug mechanism. Code identifiers expected — function names, file paths, variable names, config constants. Walk the cause chain end-to-end.

### 4. Why it produced the symptom

Link root cause to symptom. Often non-obvious in this project:
- Bug in calibration → stale EMA → wrong TDS → automator overdoses
- Missing NVS persist → reboot → duplicate feed
- Stack overflow in TaskControl → WDT reset → all outputs off

### 5. Fix _(mandatory)_

What changed and **why this change addresses the root cause** rather than hiding the symptom.

### 6. How it was found

Short. The debugging path:
- What repro made it deterministic
- Tools used (serial log, native test, CLI commands, code trace)
- Hypotheses tried and rejected (one-line reason each)
- The single experiment that confirmed the cause

### 7. Why it slipped through

What allowed this bug to reach the system:
- No native test for this path
- Latent code (correct when written, broken by later config change)
- Hardware-dependent (only triggers with real sensors)
- NVS state not tested across reboot cycles

### 8. Validation _(mandatory)_

How we know the fix works:
- `pio run -e production` passes
- Native test passes (`pio test -e native -f test_xxx`)
- Hardware verified on real board
- State what was NOT tested

### 9. Action items

Concrete next-steps. Each: what + tracking.
- Regression test added
- Config guard added
- CHANGELOG updated

If none needed: *"None — the fix is sufficient."*

## Rules

- Refuse to draft without all four required inputs.
- Never invent root cause or validation. If facts aren't there, ask.
- Code identifiers are first-class (function names, file paths, constants).
- Blameless — describe gaps and bugs, never people.
- State validation coverage honestly.
