/**
 * @file waterSystem.cpp
 * @brief Water circulation and refill controller
 */

#include "waterSystem.h"
#include "config.h"
#include "logger.h"
#include <Preferences.h>
#include <string.h>

static Preferences _prefs;

static WaterSystemConfig _config = {
    WATER_CIRCULATION_DEFAULT_ENABLED,
    WATER_REFILL_DEFAULT_ENABLED,
    false,
    WATER_REFILL_MAX_RUNTIME_MS
};

static WaterSystemStatus _status = {
    WATER_STATE_DISABLED,
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

static bool _hasCirculationPump(void) {
#if PUMP_CIRCULATION_PIN >= 0
    return true;
#else
    return false;
#endif
}

static bool _hasRefillPump(void) {
#if PUMP_REFILL_PIN >= 0
    return true;
#else
    return false;
#endif
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

static bool _readConfiguredInput(int pin, uint8_t activeState) {
    if (pin < 0) {
        return false;
    }
    return digitalRead(pin) == activeState;
}

static void _setReason(const char* reason) {
    if (strncmp(_status.reason, reason, sizeof(_status.reason)) != 0) {
        snprintf(_status.reason, sizeof(_status.reason), "%s", reason);
        LOG_INFO("[WATER] %s", _status.reason);
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

static void _saveConfig(void) {
    _prefs.begin("waterSystem", false);
    _prefs.putBool("circEn", _config.circulationEnabled);
    _prefs.putBool("refillEn", _config.refillEnabled);
    _prefs.putULong("refillMax", _config.refillMaxRuntimeMs);
    _prefs.end();
}

const char* waterSystemGetStateString(WaterSystemState state) {
    switch (state) {
        case WATER_STATE_DISABLED: return "DISABLED";
        case WATER_STATE_CIRCULATION: return "CIRCULATION";
        case WATER_STATE_REFILLING: return "REFILLING";
        case WATER_STATE_BLOCKED: return "BLOCKED";
        case WATER_STATE_ALARM: return "ALARM";
        default: return "UNKNOWN";
    }
}

void waterSystemSetup(void) {
    _prefs.begin("waterSystem", true);
    _config.circulationEnabled = _prefs.getBool("circEn", WATER_CIRCULATION_DEFAULT_ENABLED);
    _config.refillEnabled = _prefs.getBool("refillEn", WATER_REFILL_DEFAULT_ENABLED);
    _config.refillMaxRuntimeMs = _prefs.getULong("refillMax", WATER_REFILL_MAX_RUNTIME_MS);
    _prefs.end();

#if PUMP_CIRCULATION_PIN >= 0
    pinMode(PUMP_CIRCULATION_PIN, OUTPUT);
    digitalWrite(PUMP_CIRCULATION_PIN, PUMP_OFF);
#endif

#if PUMP_REFILL_PIN >= 0
    pinMode(PUMP_REFILL_PIN, OUTPUT);
    digitalWrite(PUMP_REFILL_PIN, PUMP_OFF);
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

    if (!_status.hasCirculationPump) {
        _setState(WATER_STATE_BLOCKED, "Set PUMP_CIRCULATION_PIN when relay wiring is finalized");
    } else {
        _setState(WATER_STATE_DISABLED, "Water system ready");
    }
}

void waterSystemSetConfig(bool circulationEnabled, bool refillEnabled, unsigned long refillMaxRuntimeMs) {
    _config.circulationEnabled = circulationEnabled;
    _config.refillEnabled = refillEnabled;
    _config.refillMaxRuntimeMs = refillMaxRuntimeMs;
    _saveConfig();
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
}

void waterSystemClearAlarm(void) {
    _alarmLatched = false;
    _refillStartMs = 0;
    _setState(WATER_STATE_DISABLED, "Alarm cleared");
}

void waterSystemGetStatus(WaterSystemStatus* status) {
    if (status) {
        *status = _status;
    }
}

void waterSystemLoop(void) {
    _status.hasCirculationPump = _hasCirculationPump();
    _status.hasRefillPump = _hasRefillPump();
    _status.hasLevelSensors = _hasLevelSensors();
    _status.hasOverflowSensor = _hasOverflowSensor();
    _status.levelLow = _readConfiguredInput(SUMP_LEVEL_LOW_PIN, WATER_LEVEL_TRIGGER_STATE);
    _status.levelHigh = _readConfiguredInput(SUMP_LEVEL_HIGH_PIN, WATER_LEVEL_TRIGGER_STATE);
    _status.overflowAlarm = _readConfiguredInput(FISH_TANK_OVERFLOW_PIN, OVERFLOW_SENSOR_TRIGGER_STATE);

    if (_status.overflowAlarm) {
        _alarmLatched = true;
        _setState(WATER_STATE_ALARM, "Fish tank overflow alarm triggered");
    }

    if (_status.hasLevelSensors && _status.levelLow && _status.levelHigh) {
        _alarmLatched = true;
        _setState(WATER_STATE_ALARM, "Sump level sensors disagree: low and high active together");
    }

    bool circulationDesired = _config.circulationEnabled;
    bool refillDesired = false;
    bool blocked = false;

    if (_config.manualRefill) {
        if (!_status.hasRefillPump) {
            blocked = true;
            _setState(WATER_STATE_BLOCKED, "Manual refill requested but PUMP_REFILL_PIN is not set");
        } else {
            refillDesired = true;
        }
    } else if (_config.refillEnabled) {
        if (!_status.hasRefillPump) {
            blocked = true;
            _setState(WATER_STATE_BLOCKED, "Refill enabled but PUMP_REFILL_PIN is not set");
        } else if (!_status.hasLevelSensors) {
            blocked = true;
            _setState(WATER_STATE_BLOCKED, "Refill enabled but sump level sensors are not configured");
        } else if (_status.levelHigh) {
            refillDesired = false;
            _refillStartMs = 0;
        } else if (_status.levelLow) {
            refillDesired = true;
        } else {
            refillDesired = false;
            _refillStartMs = 0;
        }
    } else {
        _refillStartMs = 0;
    }

    if (refillDesired) {
        if (_refillStartMs == 0) {
            _refillStartMs = millis();
        }
        if ((millis() - _refillStartMs) >= _config.refillMaxRuntimeMs) {
            _alarmLatched = true;
            refillDesired = false;
            _config.manualRefill = false;
            _setState(WATER_STATE_ALARM, "Refill timeout reached before sump became full");
        }
    }

    if (_alarmLatched) {
        circulationDesired = false;
        refillDesired = false;
    }

    _status.circulationOutput = circulationDesired && _status.hasCirculationPump;
    _status.refillOutput = refillDesired && _status.hasRefillPump;
    _status.alarmActive = _alarmLatched;

    _writePumpOutput(PUMP_CIRCULATION_PIN, _status.circulationOutput);
    _writePumpOutput(PUMP_REFILL_PIN, _status.refillOutput);

    if (_alarmLatched) {
        _setState(WATER_STATE_ALARM, _status.reason);
        return;
    }

    if (blocked) {
        return;
    }

    if (_status.refillOutput) {
        if (_config.manualRefill) {
            _setState(WATER_STATE_REFILLING, "Manual refill is active");
        } else {
            _setState(WATER_STATE_REFILLING, "Sump low level detected - refilling");
        }
    } else if (_status.circulationOutput) {
        _setState(WATER_STATE_CIRCULATION, "Circulation pump is running");
    } else {
        _setState(WATER_STATE_DISABLED, "Water system idle");
    }
}