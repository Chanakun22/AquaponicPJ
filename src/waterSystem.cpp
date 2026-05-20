/**
 * @file waterSystem.cpp
 * @brief Water circulation and refill controller
 */

#include "waterSystem.h"
#include "config.h"
#include "logger.h"
#include <Preferences.h>
#include <time.h>
#include <string.h>

static Preferences _prefs;

static WaterSystemConfig _config = {
    WATER_CIRCULATION_DEFAULT_ENABLED,
    WATER_REFILL_DEFAULT_ENABLED,
    false,
    WATER_REFILL_MAX_RUNTIME_MS,
    WATER_REFILL_MIN_INTERVAL_MS,
    (WaterRefillRoute)WATER_REFILL_ROUTE_DEFAULT,
    WATER_ALLOW_DIRECT_SUMP_REFILL_DEFAULT,
    WATER_FISH_REFILL_INTERVAL_MS,
    WATER_FISH_REFILL_MAX_RUNTIME_MS
};

static WaterSystemStatus _status = {
    WATER_STATE_IDLE,
    WATER_REFILL_ROUTE_NONE,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    true,
    0,
    true,
    0,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    "Water system not initialized"
};

static bool _alarmLatched = false;
static unsigned long _refillStartMs = 0;
static unsigned long _lastMixRefillStopMs = 0;
static unsigned long _lastFishRefillStopMs = 0;
static unsigned long _lastDilutionEventMs = 0;
static WaterRefillRoute _lastDilutionRoute = WATER_REFILL_ROUTE_NONE;
static bool _refillWasActive = false;
static WaterRefillRoute _lastActiveRefillRoute = WATER_REFILL_ROUTE_NONE;

typedef struct {
    bool initialized;
    bool rawValue;
    bool stableValue;
    unsigned long lastChangeMs;
} DebouncedWaterInput;

static DebouncedWaterInput _sumpLowInput = {false, false, false, 0};
static DebouncedWaterInput _sumpHighInput = {false, false, false, 0};
static DebouncedWaterInput _overflowInput = {false, false, false, 0};

#ifdef WATER_SYSTEM_TEST_OVERRIDES
static bool _testOverrideHasCirculationPumpSet = false;
static bool _testOverrideHasCirculationPump = false;

static void waterSystemTestSetCirculationPumpPresent(bool present) {
    _testOverrideHasCirculationPumpSet = true;
    _testOverrideHasCirculationPump = present;
}

static void waterSystemTestResetOverrides(void) {
    _testOverrideHasCirculationPumpSet = false;
}
#endif

static const unsigned long WATER_MIX_SETTLING_MS = 120000UL;
static const unsigned long WATER_MIN_VALID_EPOCH_SEC = 1700000000UL;
static const unsigned long WATER_LEVEL_INPUT_DEBOUNCE_MS = 300UL;

typedef struct {
    unsigned long snapshotRemainingMs;
    unsigned long deadlineEpochSec;
    bool reconcileWithClock;
} PersistedWaterTimer;

static PersistedWaterTimer _persistedMixInterval = {0, 0, false};
static PersistedWaterTimer _persistedFishWait = {0, 0, false};
static PersistedWaterTimer _persistedDilutionHold = {0, 0, false};
static WaterRefillRoute _persistedDilutionRoute = WATER_REFILL_ROUTE_NONE;
static unsigned long _persistedRestoreStartMs = 0;
static bool _runtimePersistenceStored = false;

static void _copyUtf8Safe(char* destination, size_t destinationSize, const char* source) {
    if (destination == NULL || destinationSize == 0) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    size_t writeIndex = 0;
    const unsigned char* cursor = (const unsigned char*)source;

    while (*cursor != '\0' && writeIndex < (destinationSize - 1)) {
        size_t charLen = 1;

        if ((*cursor & 0x80) == 0x00) {
            charLen = 1;
        } else if ((*cursor & 0xE0) == 0xC0) {
            charLen = 2;
        } else if ((*cursor & 0xF0) == 0xE0) {
            charLen = 3;
        } else if ((*cursor & 0xF8) == 0xF0) {
            charLen = 4;
        } else {
            break;
        }

        if (writeIndex + charLen > (destinationSize - 1)) {
            break;
        }

        bool validSequence = true;
        for (size_t i = 1; i < charLen; i++) {
            if (cursor[i] == '\0' || (cursor[i] & 0xC0) != 0x80) {
                validSequence = false;
                break;
            }
        }

        if (!validSequence) {
            break;
        }

        memcpy(destination + writeIndex, cursor, charLen);
        writeIndex += charLen;
        cursor += charLen;
    }

    destination[writeIndex] = '\0';
}

static bool _fishRefillCycleActive(void) {
    return _refillWasActive
        && _lastActiveRefillRoute == WATER_REFILL_ROUTE_FISH_TANK
        && _refillStartMs != 0;
}

static void _resetPersistedTimerState(PersistedWaterTimer* timer) {
    if (!timer) {
        return;
    }

    timer->snapshotRemainingMs = 0;
    timer->deadlineEpochSec = 0;
    timer->reconcileWithClock = false;
}

static void _resetRuntimeState(void) {
    _alarmLatched = false;
    _refillStartMs = 0;
    _lastMixRefillStopMs = 0;
    _lastFishRefillStopMs = 0;
    _lastDilutionEventMs = 0;
    _lastDilutionRoute = WATER_REFILL_ROUTE_NONE;
    _refillWasActive = false;
    _lastActiveRefillRoute = WATER_REFILL_ROUTE_NONE;
    _sumpLowInput = {false, false, false, 0};
    _sumpHighInput = {false, false, false, 0};
    _overflowInput = {false, false, false, 0};

    memset(&_status, 0, sizeof(_status));
    _status.state = WATER_STATE_IDLE;
    _status.activeRoute = WATER_REFILL_ROUTE_NONE;
    _status.mixTankControlZone = false;
    _status.fishRefillReady = true;
    _copyUtf8Safe(_status.reason, sizeof(_status.reason), "Water system not initialized");
}

static bool _hasFishTankRefillPump(void) {
#if PUMP_REFILL_PIN >= 0
    return true;
#else
    return false;
#endif
}

static bool _hasMixTankRefillSolenoid(void) {
#if REFILL_ROUTE_VALVE_PIN >= 0
    return true;
#else
    return false;
#endif
}

static bool _hasCirculationPump(void) {
#ifdef WATER_SYSTEM_TEST_OVERRIDES
    if (_testOverrideHasCirculationPumpSet) {
        return _testOverrideHasCirculationPump;
    }
#endif
#if PUMP_CIRCULATION_PIN >= 0
    return true;
#else
    return false;
#endif
}

static bool _hasRefillPump(void) {
    return _hasFishTankRefillPump() || _hasMixTankRefillSolenoid();
}

static bool _hasLevelSensors(void) {
#if SUMP_LEVEL_LOW_PIN >= 0 && SUMP_LEVEL_HIGH_PIN >= 0
    return true;
#else
    return false;
#endif
}

static bool _hasOverflowSensor(void) {
#if FISH_TANK_OVERFLOW_PIN >= 0
    return true;
#else
    return false;
#endif
}

static bool _hasRouteValve(void) {
    return _hasMixTankRefillSolenoid();
}

static bool _readConfiguredInput(int pin, uint8_t activeState) {
    if (pin < 0) {
        return false;
    }
    return digitalRead(pin) == activeState;
}

static bool _readDebouncedInput(DebouncedWaterInput* input,
                                int pin,
                                uint8_t activeState,
                                unsigned long now,
                                bool activateImmediately) {
    if (!input || pin < 0) {
        return false;
    }

    bool sample = _readConfiguredInput(pin, activeState);
    if (!input->initialized) {
        input->initialized = true;
        input->rawValue = sample;
        input->stableValue = sample;
        input->lastChangeMs = now;
        return sample;
    }

    if (sample != input->rawValue) {
        input->rawValue = sample;
        input->lastChangeMs = now;
    }

    if (input->stableValue != input->rawValue) {
        if (activateImmediately && input->rawValue) {
            input->stableValue = true;
        } else if ((now - input->lastChangeMs) >= WATER_LEVEL_INPUT_DEBOUNCE_MS) {
            input->stableValue = input->rawValue;
        }
    }

    return input->stableValue;
}

static void _setReason(const char* reason) {
    const char* nextReason = reason != NULL ? reason : "";

    if (strncmp(_status.reason, nextReason, sizeof(_status.reason)) != 0) {
        _copyUtf8Safe(_status.reason, sizeof(_status.reason), nextReason);
        if (_status.state != WATER_STATE_WAIT_REFILL_INTERVAL
                && _status.state != WATER_STATE_MIX_TANK_SETTLING) {
            LOG_INFO("[WATER] %s", _status.reason);
        }
    }
}

static void _setState(WaterSystemState state, const char* reason) {
    if (_status.state != state) {
        _status.state = state;
        LOG_INFO("[WATER] State -> %s", waterSystemGetStateString(state));
    }
    _setReason(reason);
}

static void _writePumpOutput(int pin, bool enabled) {
    if (pin < 0) {
        return;
    }
    digitalWrite(pin, enabled ? PUMP_ON : PUMP_OFF);
}

static void _writeRouteValve(WaterRefillRoute route) {
#if REFILL_ROUTE_VALVE_PIN >= 0
    digitalWrite(REFILL_ROUTE_VALVE_PIN,
                 route == WATER_REFILL_ROUTE_SUMP_DIRECT
                     ? REFILL_ROUTE_TO_SUMP_STATE
                     : REFILL_ROUTE_TO_FISH_STATE);
#else
    (void)route;
#endif
}

static bool _routeFeedsMixTank(WaterRefillRoute route) {
    return route == WATER_REFILL_ROUTE_SUMP_DIRECT || route == WATER_REFILL_ROUTE_FISH_TANK;
}

static bool _routeAvailable(WaterRefillRoute route) {
    switch (route) {
        case WATER_REFILL_ROUTE_FISH_TANK:
            return _hasFishTankRefillPump() && _hasRouteValve();
        case WATER_REFILL_ROUTE_SUMP_DIRECT:
            return _hasMixTankRefillSolenoid();
        default:
            return false;
    }
}

static unsigned long _getRemainingIntervalMs(unsigned long now,
                                             unsigned long lastEventMs,
                                             unsigned long intervalMs) {
    if (lastEventMs == 0 || intervalMs == 0) {
        return 0;
    }

    unsigned long elapsed = now - lastEventMs;
    if (elapsed >= intervalMs) {
        return 0;
    }

    return intervalMs - elapsed;
}

static unsigned long _getFishRefillWaitRemainingMs(unsigned long now) {
    if (!_routeAvailable(WATER_REFILL_ROUTE_FISH_TANK)) {
        return 0;
    }

    return _getRemainingIntervalMs(now, _lastFishRefillStopMs, _config.fishRefillIntervalMs);
}

static unsigned long _currentEpochSec(void) {
    time_t now = time(NULL);
    if (now < (time_t)WATER_MIN_VALID_EPOCH_SEC) {
        return 0;
    }

    return (unsigned long)now;
}

static unsigned long _deadlineFromRemainingMs(unsigned long remainingMs, unsigned long epochNow) {
    if (remainingMs == 0 || epochNow == 0) {
        return 0;
    }

    return epochNow + ((remainingMs + 999UL) / 1000UL);
}

static unsigned long _remainingFromPersistedTimer(const PersistedWaterTimer* timer, unsigned long now) {
    if (!timer) {
        return 0;
    }

    unsigned long epochNow = _currentEpochSec();
    if (timer->deadlineEpochSec != 0 && epochNow != 0) {
        if (timer->deadlineEpochSec <= epochNow) {
            return 0;
        }

        return (timer->deadlineEpochSec - epochNow) * 1000UL;
    }

    if (timer->snapshotRemainingMs == 0) {
        return 0;
    }

    unsigned long elapsedSinceRestore = now - _persistedRestoreStartMs;
    if (elapsedSinceRestore >= timer->snapshotRemainingMs) {
        return 0;
    }

    return timer->snapshotRemainingMs - elapsedSinceRestore;
}

static void _writePersistedTimer(const char* remainingKey,
                                 const char* deadlineKey,
                                 unsigned long remainingMs,
                                 unsigned long epochNow) {
    _prefs.putULong(remainingKey, remainingMs);
    _prefs.putULong(deadlineKey, _deadlineFromRemainingMs(remainingMs, epochNow));
}

static void _clearRuntimePersistenceStorage(void) {
    _prefs.begin("waterSystem", false);
    _writePersistedTimer("mixGapMs", "mixGapDue", 0, 0);
    _writePersistedTimer("fishWaitMs", "fishWaitDue", 0, 0);
    _writePersistedTimer("diluteMs", "diluteDue", 0, 0);
    _prefs.putUChar("diluteRt", (uint8_t)WATER_REFILL_ROUTE_NONE);
    _prefs.end();

    _runtimePersistenceStored = false;
    _resetPersistedTimerState(&_persistedMixInterval);
    _resetPersistedTimerState(&_persistedFishWait);
    _resetPersistedTimerState(&_persistedDilutionHold);
    _persistedDilutionRoute = WATER_REFILL_ROUTE_NONE;
    _persistedRestoreStartMs = millis();
}

static void _saveRuntimePersistence(void) {
    unsigned long now = millis();
    unsigned long mixIntervalRemainingMs = _getRemainingIntervalMs(now, _lastMixRefillStopMs, _config.refillMinIntervalMs);
    unsigned long fishWaitRemainingMs = _getFishRefillWaitRemainingMs(now);
    unsigned long dilutionRemainingMs = _getRemainingIntervalMs(now, _lastDilutionEventMs, WATER_MIX_SETTLING_MS);

    if (mixIntervalRemainingMs == 0) {
        mixIntervalRemainingMs = _remainingFromPersistedTimer(&_persistedMixInterval, now);
    }
    if (fishWaitRemainingMs == 0) {
        fishWaitRemainingMs = _remainingFromPersistedTimer(&_persistedFishWait, now);
    }
    if (dilutionRemainingMs == 0) {
        dilutionRemainingMs = _remainingFromPersistedTimer(&_persistedDilutionHold, now);
    }

    unsigned long epochNow = _currentEpochSec();

    _prefs.begin("waterSystem", false);
    _writePersistedTimer("mixGapMs", "mixGapDue", mixIntervalRemainingMs, epochNow);
    _writePersistedTimer("fishWaitMs", "fishWaitDue", fishWaitRemainingMs, epochNow);
    _writePersistedTimer("diluteMs", "diluteDue", dilutionRemainingMs, epochNow);
    _prefs.putUChar("diluteRt",
                    (uint8_t)(dilutionRemainingMs > 0
                        ? _lastDilutionRoute
                        : WATER_REFILL_ROUTE_NONE));
    _prefs.end();

    _runtimePersistenceStored = mixIntervalRemainingMs > 0
        || fishWaitRemainingMs > 0
        || dilutionRemainingMs > 0;
    _resetPersistedTimerState(&_persistedMixInterval);
    _resetPersistedTimerState(&_persistedFishWait);
    _resetPersistedTimerState(&_persistedDilutionHold);
    _persistedDilutionRoute = dilutionRemainingMs > 0
        ? _lastDilutionRoute
        : WATER_REFILL_ROUTE_NONE;
    _persistedRestoreStartMs = now;
}

static void _loadRuntimePersistence(void) {
    _resetPersistedTimerState(&_persistedMixInterval);
    _resetPersistedTimerState(&_persistedFishWait);
    _resetPersistedTimerState(&_persistedDilutionHold);
    _persistedDilutionRoute = WATER_REFILL_ROUTE_NONE;

    _prefs.begin("waterSystem", true);
    _persistedMixInterval.snapshotRemainingMs = _prefs.getULong("mixGapMs", 0);
    _persistedMixInterval.deadlineEpochSec = _prefs.getULong("mixGapDue", 0);
    _persistedFishWait.snapshotRemainingMs = _prefs.getULong("fishWaitMs", 0);
    _persistedFishWait.deadlineEpochSec = _prefs.getULong("fishWaitDue", 0);
    _persistedDilutionHold.snapshotRemainingMs = _prefs.getULong("diluteMs", 0);
    _persistedDilutionHold.deadlineEpochSec = _prefs.getULong("diluteDue", 0);
    _persistedDilutionRoute = (WaterRefillRoute)_prefs.getUChar("diluteRt", (uint8_t)WATER_REFILL_ROUTE_NONE);
    _prefs.end();

    unsigned long epochNow = _currentEpochSec();
    _persistedMixInterval.reconcileWithClock = _persistedMixInterval.deadlineEpochSec > 0 && epochNow == 0;
    _persistedFishWait.reconcileWithClock = _persistedFishWait.deadlineEpochSec > 0 && epochNow == 0;
    _persistedDilutionHold.reconcileWithClock = _persistedDilutionHold.deadlineEpochSec > 0 && epochNow == 0;
    _persistedRestoreStartMs = millis();
    _runtimePersistenceStored = _persistedMixInterval.snapshotRemainingMs > 0
        || _persistedMixInterval.deadlineEpochSec > 0
        || _persistedFishWait.snapshotRemainingMs > 0
        || _persistedFishWait.deadlineEpochSec > 0
        || _persistedDilutionHold.snapshotRemainingMs > 0
        || _persistedDilutionHold.deadlineEpochSec > 0;
}

static void _restorePersistedTimer(unsigned long now,
                                   PersistedWaterTimer* timer,
                                   unsigned long intervalMs,
                                   unsigned long* lastEventMs) {
    if (!timer || !lastEventMs || intervalMs == 0) {
        return;
    }

    bool hasSnapshot = timer->snapshotRemainingMs > 0;
    bool hasDeadline = timer->deadlineEpochSec > 0;
    if (!hasSnapshot && !hasDeadline && !timer->reconcileWithClock) {
        return;
    }

    unsigned long epochNow = _currentEpochSec();
    unsigned long remainingMs = _remainingFromPersistedTimer(timer, now);
    if (remainingMs == 0) {
        if (hasDeadline && epochNow == 0) {
            return;
        }

        *lastEventMs = 0;
        _resetPersistedTimerState(timer);
        return;
    }

    unsigned long clampedRemainingMs = remainingMs > intervalMs
        ? intervalMs
        : remainingMs;
    *lastEventMs = now - (intervalMs - clampedRemainingMs);

    if (hasDeadline && epochNow != 0) {
        _resetPersistedTimerState(timer);
        return;
    }

    if (hasSnapshot) {
        timer->snapshotRemainingMs = 0;
        timer->reconcileWithClock = hasDeadline;
    }
}

static void _restoreRuntimePersistence(unsigned long now) {
    _restorePersistedTimer(now, &_persistedMixInterval, _config.refillMinIntervalMs, &_lastMixRefillStopMs);
    _restorePersistedTimer(now, &_persistedFishWait, _config.fishRefillIntervalMs, &_lastFishRefillStopMs);

    if (_persistedDilutionRoute != WATER_REFILL_ROUTE_NONE
            && (_persistedDilutionHold.snapshotRemainingMs > 0
                || _persistedDilutionHold.deadlineEpochSec > 0
                || _persistedDilutionHold.reconcileWithClock)) {
        _lastDilutionRoute = _persistedDilutionRoute;
    }
    _restorePersistedTimer(now, &_persistedDilutionHold, WATER_MIX_SETTLING_MS, &_lastDilutionEventMs);

    if (_persistedDilutionHold.snapshotRemainingMs == 0
            && _persistedDilutionHold.deadlineEpochSec == 0
            && !_persistedDilutionHold.reconcileWithClock
            && _getRemainingIntervalMs(now, _lastDilutionEventMs, WATER_MIX_SETTLING_MS) == 0) {
        _persistedDilutionRoute = WATER_REFILL_ROUTE_NONE;
    }
}

static bool _hasPendingRuntimeRestore(void) {
    return _persistedMixInterval.snapshotRemainingMs > 0
        || _persistedMixInterval.deadlineEpochSec > 0
        || _persistedMixInterval.reconcileWithClock
        || _persistedFishWait.snapshotRemainingMs > 0
        || _persistedFishWait.deadlineEpochSec > 0
        || _persistedFishWait.reconcileWithClock
        || _persistedDilutionHold.snapshotRemainingMs > 0
        || _persistedDilutionHold.deadlineEpochSec > 0
        || _persistedDilutionHold.reconcileWithClock;
}

static void _maybeClearRuntimePersistence(unsigned long now) {
    if (!_runtimePersistenceStored || _hasPendingRuntimeRestore()) {
        return;
    }

    if (_getRemainingIntervalMs(now, _lastMixRefillStopMs, _config.refillMinIntervalMs) > 0) {
        return;
    }

    if (_getFishRefillWaitRemainingMs(now) > 0) {
        return;
    }

    if (_getRemainingIntervalMs(now, _lastDilutionEventMs, WATER_MIX_SETTLING_MS) > 0) {
        return;
    }

    _clearRuntimePersistenceStorage();
}

static void _formatDurationTh(unsigned long durationMs, char* buffer, size_t bufferSize) {
    unsigned long totalSeconds = (durationMs + 500UL) / 1000UL;
    unsigned long days = totalSeconds / 86400UL;
    unsigned long hours = (totalSeconds % 86400UL) / 3600UL;
    unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
    unsigned long seconds = totalSeconds % 60UL;

    if (bufferSize == 0) {
        return;
    }

    if (days > 0) {
        if (hours > 0) {
            snprintf(buffer, bufferSize, "%lu วัน %lu ชั่วโมง", days, hours);
        } else if (minutes > 0) {
            snprintf(buffer, bufferSize, "%lu วัน %lu นาที", days, minutes);
        } else {
            snprintf(buffer, bufferSize, "%lu วัน", days);
        }
        return;
    }

    if (hours > 0) {
        if (minutes > 0) {
            snprintf(buffer, bufferSize, "%lu ชั่วโมง %lu นาที", hours, minutes);
        } else {
            snprintf(buffer, bufferSize, "%lu ชั่วโมง", hours);
        }
        return;
    }

    if (minutes > 0) {
        if (seconds > 0) {
            snprintf(buffer, bufferSize, "%lu นาที %lu วินาที", minutes, seconds);
        } else {
            snprintf(buffer, bufferSize, "%lu นาที", minutes);
        }
        return;
    }

    snprintf(buffer, bufferSize, "%lu วินาที", totalSeconds);
}

static bool _isFishRefillReady(unsigned long now, bool overflowActive) {
    return _routeAvailable(WATER_REFILL_ROUTE_FISH_TANK)
    && _hasOverflowSensor()
        && !overflowActive
        && _getFishRefillWaitRemainingMs(now) == 0;
}

static WaterRefillRoute _resolveRefillRouteForNow(unsigned long now, bool overflowActive) {
    switch (_config.preferredRoute) {
        case WATER_REFILL_ROUTE_FISH_TANK:
            return _isFishRefillReady(now, overflowActive) ? WATER_REFILL_ROUTE_FISH_TANK : WATER_REFILL_ROUTE_NONE;

        case WATER_REFILL_ROUTE_SUMP_DIRECT:
            return _routeAvailable(WATER_REFILL_ROUTE_SUMP_DIRECT) ? WATER_REFILL_ROUTE_SUMP_DIRECT : WATER_REFILL_ROUTE_NONE;

        case WATER_REFILL_ROUTE_AUTO:
            if (_isFishRefillReady(now, overflowActive)) {
                return WATER_REFILL_ROUTE_FISH_TANK;
            }
            if (_config.allowDirectSumpRefill && _routeAvailable(WATER_REFILL_ROUTE_SUMP_DIRECT)) {
                return WATER_REFILL_ROUTE_SUMP_DIRECT;
            }
            return WATER_REFILL_ROUTE_NONE;

        case WATER_REFILL_ROUTE_NONE:
        default:
            return WATER_REFILL_ROUTE_NONE;
    }
}

static void _updateDerivedStatus(unsigned long now) {
    bool settlingActive = false;
    unsigned long dilutionHoldRemainingMs = 0;

    if (_lastDilutionEventMs != 0) {
        unsigned long dilutionElapsedMs = now - _lastDilutionEventMs;
        if (dilutionElapsedMs < WATER_MIX_SETTLING_MS) {
            settlingActive = !_status.refillOutput;
            dilutionHoldRemainingMs = WATER_MIX_SETTLING_MS - dilutionElapsedMs;
        }
    }

    _status.circulationPumpOutput = _status.circulationOutput;
    bool dilutionActive = _status.refillOutput || settlingActive;
    bool controlZoneReady = _status.circulationPumpOutput
        && !dilutionActive
        && !_status.alarmActive
        && !_status.routeBlocked;

    _status.mixTankSettlingActive = settlingActive;
    _status.waterDilutionActive = dilutionActive;
    _status.mixTankControlZone = controlZoneReady;
    _status.dilutionHoldRemainingMs = dilutionHoldRemainingMs;
}

static void _sanitizeConfig(void) {
    if (_config.refillMaxRuntimeMs == 0 || _config.refillMaxRuntimeMs > WATER_REFILL_MAX_RUNTIME_MS) {
        _config.refillMaxRuntimeMs = WATER_REFILL_MAX_RUNTIME_MS;
    }

    if (_config.refillMinIntervalMs > WATER_FISH_REFILL_INTERVAL_MS) {
        _config.refillMinIntervalMs = WATER_FISH_REFILL_INTERVAL_MS;
    }

    if ((uint8_t)_config.preferredRoute > (uint8_t)WATER_REFILL_ROUTE_NONE) {
        _config.preferredRoute = (WaterRefillRoute)WATER_REFILL_ROUTE_DEFAULT;
    }

    if (_config.fishRefillIntervalMs == 0 || _config.fishRefillIntervalMs > (30UL * 24UL * 60UL * 60UL * 1000UL)) {
        _config.fishRefillIntervalMs = WATER_FISH_REFILL_INTERVAL_MS;
    }

    if (_config.fishRefillMaxRuntimeMs == 0 || _config.fishRefillMaxRuntimeMs > WATER_REFILL_MAX_RUNTIME_MS) {
        _config.fishRefillMaxRuntimeMs = WATER_FISH_REFILL_MAX_RUNTIME_MS;
    }
}

static void _saveConfig(void) {
    _sanitizeConfig();

    _prefs.begin("waterSystem", false);
    _prefs.putBool("circEn", _config.circulationEnabled);
    _prefs.putBool("refillEn", _config.refillEnabled);
    _prefs.putULong("refillMax", _config.refillMaxRuntimeMs);
    _prefs.putULong("refillMin", _config.refillMinIntervalMs);
    _prefs.putUChar("route", (uint8_t)_config.preferredRoute);
    _prefs.putBool("allowDir", _config.allowDirectSumpRefill);
    _prefs.putULong("fishInt", _config.fishRefillIntervalMs);
    _prefs.putULong("fishMax", _config.fishRefillMaxRuntimeMs);
    _prefs.end();
}

const char* waterSystemGetStateString(WaterSystemState state) {
    switch (state) {
        case WATER_STATE_IDLE: return "IDLE";
        case WATER_STATE_MIX_TANK_REFILL: return "MIX_TANK_REFILL";
        case WATER_STATE_WAIT_REFILL_INTERVAL: return "WAIT_REFILL_INTERVAL";
        case WATER_STATE_MIX_TANK_SETTLING: return "MIX_TANK_SETTLING";
        case WATER_STATE_FISH_TANK_REFILL: return "FISH_TANK_REFILL";
        case WATER_STATE_BLOCKED: return "BLOCKED";
        case WATER_STATE_ALARM: return "ALARM";
        default: return "UNKNOWN";
    }
}

const char* waterSystemGetStateLabelTh(WaterSystemState state) {
    switch (state) {
        case WATER_STATE_IDLE: return "พร้อมทำงาน";
        case WATER_STATE_MIX_TANK_REFILL: return "เติมถังน้ำผสม";
        case WATER_STATE_WAIT_REFILL_INTERVAL: return "รอช่วงกันเติมถี่";
        case WATER_STATE_MIX_TANK_SETTLING: return "รอให้น้ำในถังผสมนิ่ง";
        case WATER_STATE_FISH_TANK_REFILL: return "เติมผ่านตู้ปลา";
        case WATER_STATE_BLOCKED: return "ถูกบล็อก";
        case WATER_STATE_ALARM: return "แจ้งเตือน";
        default: return "ไม่ทราบสถานะ";
    }
}

const char* waterSystemGetRouteString(WaterRefillRoute route) {
    switch (route) {
        case WATER_REFILL_ROUTE_AUTO: return "AUTO";
        case WATER_REFILL_ROUTE_FISH_TANK: return "FISH_TANK";
        case WATER_REFILL_ROUTE_SUMP_DIRECT: return "SUMP_DIRECT";
        case WATER_REFILL_ROUTE_NONE: return "NONE";
        default: return "UNKNOWN";
    }
}

void waterSystemSetup(void) {
    _resetRuntimeState();

    _prefs.begin("waterSystem", true);
    _config.circulationEnabled = _prefs.getBool("circEn", WATER_CIRCULATION_DEFAULT_ENABLED);
    _config.refillEnabled = _prefs.getBool("refillEn", WATER_REFILL_DEFAULT_ENABLED);
    _config.refillMaxRuntimeMs = _prefs.getULong("refillMax", WATER_REFILL_MAX_RUNTIME_MS);
    _config.refillMinIntervalMs = _prefs.getULong("refillMin", WATER_REFILL_MIN_INTERVAL_MS);
    _config.preferredRoute = (WaterRefillRoute)_prefs.getUChar("route", WATER_REFILL_ROUTE_DEFAULT);
    _config.allowDirectSumpRefill = _prefs.getBool("allowDir", WATER_ALLOW_DIRECT_SUMP_REFILL_DEFAULT);
    _config.fishRefillIntervalMs = _prefs.getULong("fishInt", WATER_FISH_REFILL_INTERVAL_MS);
    _config.fishRefillMaxRuntimeMs = _prefs.getULong("fishMax", WATER_FISH_REFILL_MAX_RUNTIME_MS);
    _prefs.end();
    _sanitizeConfig();
    _loadRuntimePersistence();

#if PUMP_CIRCULATION_PIN >= 0
    pinMode(PUMP_CIRCULATION_PIN, OUTPUT);
    digitalWrite(PUMP_CIRCULATION_PIN, PUMP_OFF);
#endif

#if PUMP_REFILL_PIN >= 0
    pinMode(PUMP_REFILL_PIN, OUTPUT);
    digitalWrite(PUMP_REFILL_PIN, PUMP_OFF);
#endif

#if REFILL_ROUTE_VALVE_PIN >= 0
    pinMode(REFILL_ROUTE_VALVE_PIN, OUTPUT);
    digitalWrite(REFILL_ROUTE_VALVE_PIN, REFILL_ROUTE_TO_FISH_STATE);
#endif

#if SUMP_LEVEL_LOW_PIN >= 0
    pinMode(SUMP_LEVEL_LOW_PIN, INPUT_PULLUP);
#endif

#if SUMP_LEVEL_HIGH_PIN >= 0
    pinMode(SUMP_LEVEL_HIGH_PIN, INPUT_PULLUP);
#endif

#if FISH_TANK_OVERFLOW_PIN >= 0
    pinMode(FISH_TANK_OVERFLOW_PIN, INPUT_PULLUP);
#endif

    _status.hasCirculationPump = _hasCirculationPump();
    _status.hasRefillPump = _hasRefillPump();
    _status.hasLevelSensors = _hasLevelSensors();
    _status.hasOverflowSensor = _hasOverflowSensor();
    _status.hasRouteValve = _hasRouteValve();

    if (!_status.hasCirculationPump) {
        _setState(WATER_STATE_BLOCKED, "ยังไม่กำหนดขาปั๊มหมุนน้ำหลัก");
    } else if (!_status.hasRefillPump) {
        _setState(WATER_STATE_BLOCKED, "ยังไม่กำหนด actuator สำหรับเติมน้ำ");
    } else {
        _setState(WATER_STATE_IDLE, "ระบบน้ำพร้อมทำงาน");
    }
}

void waterSystemSetConfig(bool circulationEnabled,
                          bool refillEnabled,
                          unsigned long refillMaxRuntimeMs,
                          unsigned long refillMinIntervalMs,
                          WaterRefillRoute preferredRoute,
                          bool allowDirectSumpRefill,
                          unsigned long fishRefillIntervalMs,
                          unsigned long fishRefillMaxRuntimeMs) {
    _config.circulationEnabled = circulationEnabled;
    _config.refillEnabled = refillEnabled;
    _config.refillMaxRuntimeMs = refillMaxRuntimeMs;
    _config.refillMinIntervalMs = refillMinIntervalMs;
    _config.preferredRoute = preferredRoute;
    _config.allowDirectSumpRefill = allowDirectSumpRefill;
    _config.fishRefillIntervalMs = fishRefillIntervalMs;
    _config.fishRefillMaxRuntimeMs = fishRefillMaxRuntimeMs;
    _sanitizeConfig();
    _saveConfig();
    _saveRuntimePersistence();
}

void waterSystemGetConfig(WaterSystemConfig* config) {
    if (config) {
        *config = _config;
    }
}

void waterSystemSetManualRefill(bool enabled) {
    _config.manualRefill = enabled;
    if (!enabled) {
        _refillStartMs = 0;
    }
}

void waterSystemSetCirculationEnabled(bool enabled) {
    _config.circulationEnabled = enabled;
    _saveConfig();
    _saveRuntimePersistence();
}

void waterSystemSetPreferredRoute(WaterRefillRoute route) {
    _config.preferredRoute = route;
    _sanitizeConfig();
    _saveConfig();
    _saveRuntimePersistence();
}

void waterSystemSetAllowDirectSumpRefill(bool enabled) {
    _config.allowDirectSumpRefill = enabled;
    _sanitizeConfig();
    _saveConfig();
    _saveRuntimePersistence();
}

void waterSystemClearAlarm(void) {
    _alarmLatched = false;
    _refillStartMs = 0;
    _refillWasActive = false;
    _lastActiveRefillRoute = WATER_REFILL_ROUTE_NONE;
    _lastDilutionEventMs = 0;
    _lastDilutionRoute = WATER_REFILL_ROUTE_NONE;
    _status.activeRoute = WATER_REFILL_ROUTE_NONE;
    _status.routeBlocked = false;
    _setState(WATER_STATE_IDLE, "ล้างสถานะแจ้งเตือนแล้ว");
    _saveRuntimePersistence();
}

void waterSystemGetStatus(WaterSystemStatus* status) {
    if (status) {
        *status = _status;
    }
}

void waterSystemLoop(void) {
    unsigned long now = millis();
    char stateReason[sizeof(_status.reason)];
    char durationText[48];
    stateReason[0] = '\0';
    durationText[0] = '\0';

    _restoreRuntimePersistence(now);

    bool waitingForInterval = false;
    unsigned long mixIntervalRemainingMs = _getRemainingIntervalMs(now, _lastMixRefillStopMs, _config.refillMinIntervalMs);
    unsigned long fishIntervalRemainingMs = _getFishRefillWaitRemainingMs(now);

    _status.hasCirculationPump = _hasCirculationPump();
    _status.hasRefillPump = _hasRefillPump();
    _status.hasLevelSensors = _hasLevelSensors();
    _status.hasOverflowSensor = _hasOverflowSensor();
    _status.hasRouteValve = _hasRouteValve();
    _status.levelLow = _readDebouncedInput(&_sumpLowInput,
                                           SUMP_LEVEL_LOW_PIN,
                                           WATER_LEVEL_TRIGGER_STATE,
                                           now,
                                           false);
    _status.levelHigh = _readDebouncedInput(&_sumpHighInput,
                                            SUMP_LEVEL_HIGH_PIN,
                                            WATER_LEVEL_TRIGGER_STATE,
                                            now,
                                            true);
    _status.overflowAlarm = _readDebouncedInput(&_overflowInput,
                                                FISH_TANK_OVERFLOW_PIN,
                                                OVERFLOW_SENSOR_TRIGGER_STATE,
                                                now,
                                                true);
    _status.routeBlocked = false;
    _status.activeRoute = WATER_REFILL_ROUTE_NONE;
    _status.routeValveOutput = false;
    _status.circulationPumpOutput = false;
    _status.fishTankRefillOutput = false;
    _status.mixTankRefillOutput = false;
    _status.waterDilutionActive = false;
    _status.mixTankSettlingActive = false;
    _status.mixTankControlZone = false;
    _status.dilutionHoldRemainingMs = 0;
    _status.fishRefillReady = _isFishRefillReady(now, _status.overflowAlarm);
    _status.fishRefillWaitRemainingMs = fishIntervalRemainingMs;
    bool circulationDesired = _config.circulationEnabled;
    bool fishRefillCycleActive = _fishRefillCycleActive();
    bool refillDesired = false;
    bool blocked = false;
    WaterRefillRoute desiredRoute = WATER_REFILL_ROUTE_NONE;

    if (!_status.hasCirculationPump) {
        circulationDesired = false;
        _refillStartMs = 0;
        blocked = true;
        _setState(WATER_STATE_BLOCKED, "ยังไม่กำหนดขาปั๊มหมุนน้ำหลัก");
    } else if (_config.manualRefill) {
        if (!_status.hasRefillPump) {
            blocked = true;
            _setState(WATER_STATE_BLOCKED, "สั่งเติมด้วยมือแต่ยังไม่กำหนด actuator เติมน้ำ");
        } else {
            refillDesired = true;
        }
    } else if (_config.refillEnabled) {
        if (!_status.hasRefillPump) {
            blocked = true;
            _setState(WATER_STATE_BLOCKED, "เปิดเติมอัตโนมัติแต่ยังไม่กำหนด actuator เติมน้ำ");
        } else if (!_status.hasLevelSensors) {
            blocked = true;
            _setState(WATER_STATE_BLOCKED, "เปิดเติมอัตโนมัติแต่ยังไม่มีเซ็นเซอร์ low/high ของถังผสม");
        } else if (_status.levelHigh) {
            refillDesired = false;
            _refillStartMs = 0;
        } else if (fishRefillCycleActive) {
            refillDesired = true;
        } else if (_status.levelLow) {
            if (mixIntervalRemainingMs > 0 && _lastMixRefillStopMs != 0) {
                waitingForInterval = true;
                _formatDurationTh(mixIntervalRemainingMs, durationText, sizeof(durationText));
                snprintf(stateReason,
                         sizeof(stateReason),
                         "ถังผสมยังอยู่ในช่วงกันเติมถี่ (%s)",
                         durationText);
            } else {
                refillDesired = true;
            }
        } else {
            refillDesired = false;
            _refillStartMs = 0;
        }
    } else {
        _refillStartMs = 0;
    }

    if (refillDesired) {
        if (_refillStartMs == 0) {
            _refillStartMs = now;
        }

        desiredRoute = _resolveRefillRouteForNow(now, _status.overflowAlarm);

        if (desiredRoute == WATER_REFILL_ROUTE_NONE) {
            refillDesired = false;

            if (_routeAvailable(WATER_REFILL_ROUTE_FISH_TANK)
                    && (_config.preferredRoute == WATER_REFILL_ROUTE_FISH_TANK
                        || _config.preferredRoute == WATER_REFILL_ROUTE_AUTO)
                    && (fishIntervalRemainingMs > 0 || _status.overflowAlarm)) {
                if (_config.manualRefill
                        && _refillWasActive
                        && _lastActiveRefillRoute == WATER_REFILL_ROUTE_FISH_TANK) {
                    _config.manualRefill = false;
                }
                waitingForInterval = true;
                if (_status.overflowAlarm) {
                    snprintf(stateReason,
                             sizeof(stateReason),
                             "หยุดเติมตู้ปลา เพราะ overflow sensor ยัง active อยู่");
                } else {
                    _formatDurationTh(fishIntervalRemainingMs, durationText, sizeof(durationText));
                    snprintf(stateReason,
                             sizeof(stateReason),
                             "รอรอบเติมตู้ปลาถัดไป (%s)",
                             durationText);
                }
            } else {
                blocked = true;
                _status.routeBlocked = true;
                _setState(WATER_STATE_BLOCKED, "ไม่มีเส้นทางเติมที่ใช้งานได้ตาม config ปัจจุบัน");
            }
        }

        if (refillDesired && desiredRoute == WATER_REFILL_ROUTE_SUMP_DIRECT) {
            bool mixTankHighReached = _status.hasLevelSensors && _status.levelHigh;
            if (mixTankHighReached) {
                refillDesired = false;
                if (_config.manualRefill) {
                    _config.manualRefill = false;
                }
            }
        }

        if (refillDesired && desiredRoute == WATER_REFILL_ROUTE_FISH_TANK) {
            bool fishRuntimeReached = (now - _refillStartMs) >= _config.fishRefillMaxRuntimeMs;
            bool mixTankHighReached = _status.hasLevelSensors && _status.levelHigh;
            bool fishSafetyStop = _status.overflowAlarm || mixTankHighReached;
            bool fishShouldStop = fishSafetyStop || fishRuntimeReached;

            if (fishShouldStop) {
                _lastFishRefillStopMs = now;

                bool mixTankStillNeedsRefill = _status.hasLevelSensors
                    && _status.levelLow
                    && !_status.levelHigh;

                if (!fishSafetyStop
                        && !_config.manualRefill
                        && _config.preferredRoute == WATER_REFILL_ROUTE_AUTO
                        && _config.allowDirectSumpRefill
                        && mixTankStillNeedsRefill
                        && _routeAvailable(WATER_REFILL_ROUTE_SUMP_DIRECT)) {
                    desiredRoute = WATER_REFILL_ROUTE_SUMP_DIRECT;
                    _refillStartMs = now;
                } else {
                    refillDesired = false;
                    if (_config.manualRefill) {
                        _config.manualRefill = false;
                    }
                }
            }
        }

        if (refillDesired && (now - _refillStartMs) >= _config.refillMaxRuntimeMs) {
            refillDesired = false;
            _config.manualRefill = false;
            _alarmLatched = true;
            _setState(WATER_STATE_ALARM,
                      desiredRoute == WATER_REFILL_ROUTE_FISH_TANK
                          ? "เติมน้ำเข้าตู้ปลาเกินเวลาที่ตั้งไว้"
                          : "เติมถังน้ำผสมเกินเวลาที่ตั้งไว้");
        }
    }

    if (_alarmLatched) {
        circulationDesired = false;
        refillDesired = false;
        desiredRoute = WATER_REFILL_ROUTE_NONE;
    }

    _status.circulationOutput = circulationDesired && _status.hasCirculationPump;
    _status.refillOutput = refillDesired && _status.hasRefillPump;
    _status.activeRoute = _status.refillOutput ? desiredRoute : WATER_REFILL_ROUTE_NONE;
    _status.fishTankRefillOutput = _status.refillOutput && _status.activeRoute == WATER_REFILL_ROUTE_FISH_TANK;
    _status.mixTankRefillOutput = _status.refillOutput && _status.activeRoute == WATER_REFILL_ROUTE_SUMP_DIRECT;
    _status.routeValveOutput = _status.mixTankRefillOutput;
    _status.alarmActive = _alarmLatched;

    if (_status.refillOutput && _routeFeedsMixTank(_status.activeRoute)) {
        _lastDilutionEventMs = now;
        _lastDilutionRoute = _status.activeRoute;
    }

    _updateDerivedStatus(now);

    _writeRouteValve(_status.activeRoute);
    _writePumpOutput(PUMP_CIRCULATION_PIN, _status.circulationOutput);
    _writePumpOutput(PUMP_REFILL_PIN, _status.fishTankRefillOutput);

    if (_refillWasActive && !_status.refillOutput) {
        _lastMixRefillStopMs = now;
        if (_lastActiveRefillRoute == WATER_REFILL_ROUTE_FISH_TANK) {
            _lastFishRefillStopMs = now;
            _status.fishRefillReady = _isFishRefillReady(now, _status.overflowAlarm);
            _status.fishRefillWaitRemainingMs = _getFishRefillWaitRemainingMs(now);
        }
        _refillStartMs = 0;
        _saveRuntimePersistence();
    }

    if (_status.refillOutput) {
        _lastActiveRefillRoute = _status.activeRoute;
    } else if (!_refillWasActive) {
        _lastActiveRefillRoute = WATER_REFILL_ROUTE_NONE;
    }
    _refillWasActive = _status.refillOutput;

    _maybeClearRuntimePersistence(now);

    if (_alarmLatched) {
        _setState(WATER_STATE_ALARM, _status.reason);
        return;
    }

    if (blocked) {
        return;
    }

    if (waitingForInterval) {
        _setState(WATER_STATE_WAIT_REFILL_INTERVAL, stateReason[0] ? stateReason : "รอช่วงกันเติมถี่");
        return;
    }

    if (_status.refillOutput) {
        if (_config.manualRefill) {
            snprintf(stateReason,
                     sizeof(stateReason),
                     _status.activeRoute == WATER_REFILL_ROUTE_FISH_TANK
                         ? "กำลังสั่งปั๊มเติมน้ำเข้าตู้ปลาแบบสั่งด้วยมือ (หยุดเมื่อครบเวลา/overflow/high level)"
                         : "กำลังเปิดโซลินอยด์เติมน้ำเข้าถังผสมแบบสั่งด้วยมือ");
        } else {
            snprintf(stateReason,
                     sizeof(stateReason),
                     _status.activeRoute == WATER_REFILL_ROUTE_FISH_TANK
                         ? "ถังผสมระดับต่ำ ระบบกำลังปั๊มน้ำเข้าตู้ปลาแบบจำกัดเวลา/overflow/high level"
                         : "ถังผสมระดับต่ำ ระบบกำลังเปิดโซลินอยด์น้ำเข้า");
        }
        _setState(_status.activeRoute == WATER_REFILL_ROUTE_FISH_TANK
                      ? WATER_STATE_FISH_TANK_REFILL
                      : WATER_STATE_MIX_TANK_REFILL,
                  stateReason);
    } else if (_status.mixTankSettlingActive) {
        const char* dilutionRouteLabel =
            _lastDilutionRoute == WATER_REFILL_ROUTE_FISH_TANK
                ? "เติมผ่านตู้ปลา"
                : "เติมเข้าถังผสม";
        _formatDurationTh(_status.dilutionHoldRemainingMs, durationText, sizeof(durationText));
        snprintf(stateReason,
                 sizeof(stateReason),
                 "รอให้น้ำในถังผสมนิ่งหลัง%s (%s)",
                 dilutionRouteLabel,
                 durationText);
        _setState(WATER_STATE_MIX_TANK_SETTLING, stateReason);
    } else if (_status.circulationOutput) {
        _setState(WATER_STATE_IDLE, "โซนถังผสมกำลังหมุนน้ำและพร้อมทำงาน");
    } else {
        _setState(WATER_STATE_IDLE, "ระบบน้ำหยุดอยู่");
    }
}
