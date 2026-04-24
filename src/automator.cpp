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
static volatile bool _paused = false;  // HW Test pause flag (volatile for cross-core!)

static AutomatorConfig _config;
static Preferences _prefs;

static void _stopAutomatorOutputs(void) {
    digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
    digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);
}

static bool _automatorBlockedByWaterSystem(const WaterSystemStatus* waterStatus, const char** reason, const char** nextState) {
    if (waterStatus == NULL) {
        return false;
    }

    if (waterStatus->alarmActive) {
        if (reason) {
            *reason = "Blocked by water system alarm";
        }
        if (nextState) {
            *nextState = "WAIT_CLEAR_CONDITION";
        }
        return true;
    }

    if (!waterStatus->circulationPumpOutput) {
        if (reason) {
            *reason = "Blocked: mix tank circulation pump is not running";
        }
        if (nextState) {
            *nextState = "WAIT_CIRCULATION";
        }
        return true;
    }

    if (waterStatus->waterDilutionActive) {
        const char* dilutionReason = "Blocked: mix tank dilution is active";
        const char* dilutionNextState = "WAIT_MIX_SETTLING";

        if (waterStatus->mixTankRefillOutput) {
            dilutionReason = "Blocked: mix tank refill is active";
            dilutionNextState = "WAIT_MIX_REFILL_COMPLETE";
        } else if (waterStatus->fishTankRefillOutput) {
            dilutionReason = "Blocked: fish tank refill is active";
            dilutionNextState = "WAIT_FISH_REFILL_MIXING";
        } else if (waterStatus->mixTankSettlingActive) {
            dilutionReason = "Blocked: mix tank is settling after dilution";
            dilutionNextState = "WAIT_MIX_SETTLING";
        }

        if (reason) {
            *reason = dilutionReason;
        }
        if (nextState) {
            *nextState = dilutionNextState;
        }
        return true;
    }

    return false;
}

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
    _stopAutomatorOutputs();

    
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

const char* automatorGetNextStateString(void) {
    if (_paused) {
        return "WAIT_HW_TEST_END";
    }

    WaterSystemStatus waterStatus;
    waterSystemGetStatus(&waterStatus);

    const char* blockedReason = NULL;
    const char* blockedNextState = NULL;

    if (!_config.enabled) {
        return "ENABLE_AUTOMATION";
    }

    if (_automatorBlockedByWaterSystem(&waterStatus, &blockedReason, &blockedNextState)) {
        return blockedNextState ? blockedNextState : "WAIT_CONTROL_ZONE";
    }

    switch (_currentState) {
        case AUTO_STATE_DISABLED:
            return "IDLE";

        case AUTO_STATE_IDLE:
            return "EVALUATING";

        case AUTO_STATE_EVALUATING: {
            float waterTemp = tempRead();
            float tds = tdsRead(waterTemp);

            if (isnan(tds) || tds <= 5.0f) {
                return "IDLE";
            }

            return (tds < _config.targetTds) ? "DOSING_A" : "IDLE";
        }

        case AUTO_STATE_DOSING_A:
            return "DOSING_B";

        case AUTO_STATE_DOSING_B:
            return "COOLDOWN";

        case AUTO_STATE_WATER_FILL:
            return "IDLE";

        case AUTO_STATE_COOLDOWN:
            return "IDLE";

        default:
            return "UNKNOWN";
    }
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

void automatorPause(void) {
    if (!_paused) {
        _paused = true;
        // Turn off all pumps immediately
        _stopAutomatorOutputs();

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

    const char* blockedReason = NULL;
    const char* blockedNextState = NULL;
    if (_automatorBlockedByWaterSystem(&waterStatus, &blockedReason, &blockedNextState)) {
        if (_currentState != AUTO_STATE_IDLE && _currentState != AUTO_STATE_DISABLED) {
            _changeState(AUTO_STATE_IDLE, blockedReason ? blockedReason : "Blocked by control zone state");
        } else {
            _updateReason(blockedReason ? blockedReason : "Blocked by control zone state");
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
                _changeState(AUTO_STATE_EVALUATING, "Checking mix tank control zone");
            }
            break;

        case AUTO_STATE_EVALUATING: {
            float waterTemp = tempRead();
            float tds = tdsRead(waterTemp);
            
            // Validate sensor read (don't act on NAN or 0)
            if (isnan(tds) || tds <= 5.0f) {
                _changeState(AUTO_STATE_IDLE, "Mix tank sensor data invalid. Waiting.");
                break;
            }

            // Current automation controls only the mix tank TDS path.
            if (tds < _config.targetTds) {
                char reason[128];
                snprintf(reason,
                         sizeof(reason),
                         "Mix tank TDS low (%.1f < %.1f) - Pumping A",
                         tds,
                         _config.targetTds);
                _changeState(AUTO_STATE_DOSING_A, reason);
                digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_ON);
            } 
            else {
                _changeState(AUTO_STATE_IDLE, "Mix tank parameters normal");
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
