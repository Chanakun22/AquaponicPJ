---
name: code-cleanup-guidelines
description: Guidelines and procedures for identifying and removing unused code (dead code) in the Aquaponics project.
---

# Code Cleanup & Dead Code Analysis

This skill provides a systematic approach to identifying and removing unused code to keep the codebase clean and maintainable.

## 1. Dead Code Detection Strategy

Before deleting anything, you must **verify** that it is truly unused.

### 1.1 Search Before You Destroy

Always use `grep_search` (or `grep -r` in terminal) to check for references.

- **Functions:** Search for function name (e.g., `localMqttPublishLightStatus`).
- **Variables:** Search for variable name.
- **Constants:** Search for `#define` keys.

**Rule:** If a function/variable is **defined** but never **called/used** (except in its own definition/declaration), it is dead code.

### 1.2 Common Dead Code Patterns in This Project

#### 1. Legacy Pi Control Logic

Since moving logic to ESP32/Netpie, check for:

- 🔍 `pi_server/app.py`: Unused routes or functions (e.g., `publish_light_settings` - _Already removed_).
- 🔍 `src/localMqtt.cpp`: Unused subscriptions or callbacks.

#### 2. Unused Headers

- check `#include` directives. If you remove functionality, remove the header too.
- Example: If `lightController` triggers are moved, is `time.h` still needed?

#### 3. Commented-Out Blocks

- **Pattern:** Large blocks of `//` or `/* ... */` that were "temporarily" disabled.
- **Action:** Delete them. Git history preserves them if needed.

#### 4. Unused `#define` Macros in `config.h`

- Check `include/config.h` for constants that are no longer referenced in `src/`.

---

## 2. Step-by-Step Cleanup Workflow

1.  **Identify Candidate:** found a suspicious function `void foo() {}`.
2.  **Search:** `grep -r "foo" .`
3.  **Analyze Results:**
    - Only found in `foo.cpp` (definition) and `foo.h` (prototype)? -> **DELETE** 🗑️
    - Found in `main.cpp` but commented out? -> **DELETE** 🗑️
    - Found in `test/`? -> Keep (if tests are active) or Delete (if legacy test).
4.  **Remove:** Delete definition and prototype.
5.  **Build:** Run `pio run` (ESP32) or restart server (Pi) to verify no compile errors.

---

## 3. Specific Areas to Check (Current Status)

- [ ] **`src/netpie.cpp`**: Check for leftover "manual sync" functions.
- [ ] **`include/localMqtt.h`**: Check for leftover prototypes.
- [ ] **`pi_server/settings.html`**: Check for unused CSS classes or JS functions.
