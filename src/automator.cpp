/**
 * @file automator.cpp
 * @brief State Machine for Aquaponics Automation
 */

#include "automator.h"
#include "config.h"
#include "logger.h"
#include <Preferences.h>
#include "TdsSensor.h"
#include "tempSensor.h"
#include "phSensor.h"
#include "waterSystem.h"

static AutomatorState _currentState = AUTO_STATE_IDLE;
static unsigned long _stateStartTime = 0;
static unsigned long _lastCheckTime = 0;
static char _actionReason[128] = "System Starting";

static AutomatorConfig _config;
static Preferences _prefs;

static void _updateReason(const char* reason) {
    if (strncmp(_actionReason, reason, sizeof(_actionReason)) != 0) {
        snprintf(_actionReason, sizeof(_actionReason), "%s", reason);
    }
}

// Private state transition
static void _changeState(AutomatorState newState, const char* reason) {
    _currentState = newState;
    _stateStartTime = millis();
    _updateReason(reason);
    
    // Safety: turn off all pumps when changing states
    digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
    digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);

    
    LOG_INFO("[AUTOMATOR] State Changed to %s: %s", automatorGetStateString(newState), reason);
}

void automatorSetup(void) {
    LOG_INFO("Initializing Automator...");
    
    pinMode(PUMP_NUTRIENT_A_PIN, OUTPUT);
    digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
    
    pinMode(PUMP_NUTRIENT_B_PIN, OUTPUT);
    digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);
    

    
    _prefs.begin("automator", true);
    _config.enabled = _prefs.getBool("enabled", false); // Default disabled
    _config.targetTds = _prefs.getFloat("targetTds", AUTOMATOR_DEFAULT_TDS);
    _config.targetPh = _prefs.getFloat("targetPh", AUTOMATOR_DEFAULT_PH);
    _prefs.end();
    
    if (_config.enabled) {
        _changeState(AUTO_STATE_IDLE, "Started (Monitoring)");
    } else {
        _changeState(AUTO_STATE_DISABLED, "Automation Disabled");
    }
}

void automatorSetConfig(bool enabled, float targetTds, float targetPh) {
    _prefs.begin("automator", false);
    _prefs.putBool("enabled", enabled);
    _prefs.putFloat("targetTds", targetTds);
    _prefs.putFloat("targetPh", targetPh);
    _prefs.end();
    
    _config.enabled = enabled;
    _config.targetTds = targetTds;
    _config.targetPh = targetPh;
    
    if (!enabled) {
        _changeState(AUTO_STATE_DISABLED, "Turned off via UI");
    } else if (_currentState == AUTO_STATE_DISABLED) {
        _changeState(AUTO_STATE_IDLE, "Turned on via UI");
    }
    
    LOG_INFO("Automator Config Updated: Enabled=%d, Target TDS=%.1f", enabled, targetTds);
}

void automatorGetConfig(AutomatorConfig* config) {
    if (config) {
        *config = _config;
    }
}

AutomatorState automatorGetCurrentState(void) {
    return _currentState;
}

const char* automatorGetStateString(AutomatorState state) {
    switch(state) {
        case AUTO_STATE_DISABLED: return "DISABLED";
        case AUTO_STATE_IDLE: return "IDLE";
        case AUTO_STATE_EVALUATING: return "EVALUATING";
        case AUTO_STATE_DOSING_A: return "DOSING_A";
        case AUTO_STATE_DOSING_B: return "DOSING_B";
        case AUTO_STATE_WATER_FILL: return "WATER_FILL";
        case AUTO_STATE_COOLDOWN: return "COOLDOWN";
        default: return "UNKNOWN";
    }
}

const char* automatorGetActionReason(void) {
    return _actionReason;
}

int automatorGetTimeRemainingSec(void) {
    unsigned long now = millis();
    unsigned long elapsed = now - _stateStartTime;
    
    switch(_currentState) {
        case AUTO_STATE_DOSING_A:
        case AUTO_STATE_DOSING_B:
            if (elapsed < AUTOMATOR_PUMP_DOSE_MS) {
                return (AUTOMATOR_PUMP_DOSE_MS - elapsed) / 1000;
            }
            break;
        case AUTO_STATE_COOLDOWN:
            if (elapsed < AUTOMATOR_COOLDOWN_MS) {
                return (AUTOMATOR_COOLDOWN_MS - elapsed) / 1000;
            }
            break;
        case AUTO_STATE_IDLE:
            if (now - _lastCheckTime < AUTOMATOR_CHECK_INTERVAL) {
                return (AUTOMATOR_CHECK_INTERVAL - (now - _lastCheckTime)) / 1000;
            }
            return 0;
        default:
            break;
    }
    return 0;
}

static volatile bool _paused = false;  // HW Test pause flag (volatile for cross-core!)

void automatorPause(void) {
    if (!_paused) {
        _paused = true;
        // Turn off all pumps immediately
        digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
        digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);

        LOG_INFO("[AUTOMATOR] PAUSED for HW Test");
    }
}

void automatorResume(void) {
    if (_paused) {
        _paused = false;
        _changeState(AUTO_STATE_IDLE, "Resumed after HW Test");
        LOG_INFO("[AUTOMATOR] RESUMED");
    }
}

void automatorLoop(void) {
    // Skip all automation while paused (HW Test mode)
    if (_paused) return;

    WaterSystemStatus waterStatus;
    waterSystemGetStatus(&waterStatus);

    if (waterStatus.alarmActive) {
        if (_currentState != AUTO_STATE_IDLE && _currentState != AUTO_STATE_DISABLED) {
            _changeState(AUTO_STATE_IDLE, "Blocked by water system alarm");
        } else {
            _updateReason("Blocked by water system alarm");
        }
        return;
    }

    if (!waterStatus.circulationOutput) {
        if (_currentState != AUTO_STATE_IDLE && _currentState != AUTO_STATE_DISABLED) {
            _changeState(AUTO_STATE_IDLE, "Blocked: circulation pump is not running");
        } else {
            _updateReason("Blocked: circulation pump is not running");
        }
        return;
    }
    
    if (!_config.enabled) {
        if (_currentState != AUTO_STATE_DISABLED) {
            _changeState(AUTO_STATE_DISABLED, "Disabled");
        }
        return;
    }

    unsigned long now = millis();
    unsigned long elapsed = now - _stateStartTime;

    switch (_currentState) {
        case AUTO_STATE_DISABLED:
            _changeState(AUTO_STATE_IDLE, "Enabled");
            break;

        case AUTO_STATE_IDLE:
            if (now - _lastCheckTime >= AUTOMATOR_CHECK_INTERVAL) {
                _lastCheckTime = now;
                _changeState(AUTO_STATE_EVALUATING, "Checking Sensor Thresholds");
            }
            break;

        case AUTO_STATE_EVALUATING: {
            float waterTemp = tempRead();
            float tds = tdsRead(waterTemp);
            
            // Validate sensor read (don't act on NAN or 0)
            if (isnan(tds) || tds <= 5.0f) {
                _changeState(AUTO_STATE_IDLE, "Sensors Data Invalid. Waiting.");
                break;
            }

            // Logic 1: TDS is too low
            if (tds < _config.targetTds) {
                char reason[128];
                snprintf(reason, sizeof(reason), "TDS Low (%.1f < %.1f) - Pumping A", tds, _config.targetTds);
                _changeState(AUTO_STATE_DOSING_A, reason);
                digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_ON);
            } 
            else {
                _changeState(AUTO_STATE_IDLE, "All parameters normal");
            }
            break;
        }

        case AUTO_STATE_DOSING_A:
            if (elapsed >= AUTOMATOR_PUMP_DOSE_MS) {
                _changeState(AUTO_STATE_DOSING_B, "Pumping B");
                digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_ON);
            }
            break;

        case AUTO_STATE_DOSING_B:
            if (elapsed >= AUTOMATOR_PUMP_DOSE_MS) {
                _changeState(AUTO_STATE_COOLDOWN, "Resting for nutrients to mix");
            }
            break;

        case AUTO_STATE_COOLDOWN:
            if (elapsed >= AUTOMATOR_COOLDOWN_MS) {
                _changeState(AUTO_STATE_IDLE, "Cooldown finished");
            }
            break;

        default:
            _changeState(AUTO_STATE_IDLE, "Reset via Unknown State");
            break;
    }
}
