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
static bool _blockedTimingHold = false;
static unsigned long _blockedTimingStart = 0;

static AutomatorConfig _config;
static Preferences _prefs;

static unsigned long _doseMsForVolume(float volumeMl) {
    float safeVolume = volumeMl;
    if (!isfinite(safeVolume) || safeVolume < 0.0f) {
        safeVolume = 0.0f;
    }
    return (unsigned long)((safeVolume * 60000.0f / DOSING_PUMP_FLOW_RATE_ML_PER_MIN) + 0.5f);
}

static void _sanitizeConfig(void) {
    if (!isfinite(_config.targetTds) || _config.targetTds < 0.0f) {
        _config.targetTds = AUTOMATOR_DEFAULT_TDS;
    }

    if (!isfinite(_config.doseAVolumeMl) || _config.doseAVolumeMl < 0.0f || _config.doseAVolumeMl > 20.0f) {
        _config.doseAVolumeMl = AUTOMATOR_DOSE_A_VOLUME_ML;
    }

    if (!isfinite(_config.doseBVolumeMl) || _config.doseBVolumeMl < 0.0f || _config.doseBVolumeMl > 20.0f) {
        _config.doseBVolumeMl = AUTOMATOR_DOSE_B_VOLUME_ML;
    }

    if (_config.mixAfterAMs < 30000UL || _config.mixAfterAMs > 60UL * 60UL * 1000UL) {
        _config.mixAfterAMs = AUTOMATOR_MIX_AFTER_A_MS;
    }

    if (_config.postDoseMixMs < 60000UL || _config.postDoseMixMs > 6UL * 60UL * 60UL * 1000UL) {
        _config.postDoseMixMs = AUTOMATOR_POST_DOSE_MIX_MS;
    }

    if (!isfinite(_config.tdsHysteresisPpm) || _config.tdsHysteresisPpm < 0.0f || _config.tdsHysteresisPpm > 300.0f) {
        _config.tdsHysteresisPpm = AUTOMATOR_TDS_HYSTERESIS_PPM;
    }
}

static void _stopAutomatorOutputs(void) {
    digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
    digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);
}

static bool _tdsNeedsDose(float tds) {
    float triggerTds = _config.targetTds - _config.tdsHysteresisPpm;
    if (triggerTds < 0.0f) {
        triggerTds = 0.0f;
    }
    return tds < triggerTds;
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
    _config.doseAVolumeMl = _prefs.getFloat("doseAVol", AUTOMATOR_DOSE_A_VOLUME_ML);
    _config.doseBVolumeMl = _prefs.getFloat("doseBVol", AUTOMATOR_DOSE_B_VOLUME_ML);
    _config.mixAfterAMs = _prefs.getULong("mixAfterA", AUTOMATOR_MIX_AFTER_A_MS);
    _config.postDoseMixMs = _prefs.getULong("postDoseMix", AUTOMATOR_POST_DOSE_MIX_MS);
    _config.tdsHysteresisPpm = _prefs.getFloat("tdsHyst", AUTOMATOR_TDS_HYSTERESIS_PPM);
    _prefs.end();
    _sanitizeConfig();
    
    if (_config.enabled) {
        _changeState(AUTO_STATE_IDLE, "Started (Monitoring)");
    } else {
        _changeState(AUTO_STATE_DISABLED, "Automation Disabled");
    }
}

void automatorSetConfig(bool enabled,
                       float targetTds,
                       float doseAVolumeMl,
                       float doseBVolumeMl,
                       unsigned long mixAfterAMs,
                       unsigned long postDoseMixMs,
                       float tdsHysteresisPpm) {
    _config.enabled = enabled;
    _config.targetTds = targetTds;
    _config.doseAVolumeMl = doseAVolumeMl;
    _config.doseBVolumeMl = doseBVolumeMl;
    _config.mixAfterAMs = mixAfterAMs;
    _config.postDoseMixMs = postDoseMixMs;
    _config.tdsHysteresisPpm = tdsHysteresisPpm;
    _sanitizeConfig();

    _prefs.begin("automator", false);
    _prefs.putBool("enabled", _config.enabled);
    _prefs.putFloat("targetTds", _config.targetTds);
    _prefs.putFloat("doseAVol", _config.doseAVolumeMl);
    _prefs.putFloat("doseBVol", _config.doseBVolumeMl);
    _prefs.putULong("mixAfterA", _config.mixAfterAMs);
    _prefs.putULong("postDoseMix", _config.postDoseMixMs);
    _prefs.putFloat("tdsHyst", _config.tdsHysteresisPpm);
    _prefs.end();
    
    if (!_config.enabled) {
        _changeState(AUTO_STATE_DISABLED, "Turned off via UI");
    } else if (_currentState == AUTO_STATE_DISABLED) {
        _changeState(AUTO_STATE_IDLE, "Turned on via UI");
    }
    
    LOG_INFO(
        "Automator Config Updated: Enabled=%d, Target TDS=%.1f, doseA=%.2f mL, doseB=%.2f mL, mixA=%lu ms, postMix=%lu ms, hyst=%.1f",
        _config.enabled,
        _config.targetTds,
        _config.doseAVolumeMl,
        _config.doseBVolumeMl,
        _config.mixAfterAMs,
        _config.postDoseMixMs,
        _config.tdsHysteresisPpm
    );
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
        case AUTO_STATE_MIXING_AFTER_A: return "MIXING_AFTER_A";
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
            float tds = tdsGetLastValue();

            if (isnan(tds) || tds <= 5.0f) {
                return "IDLE";
            }

            return _tdsNeedsDose(tds) ? "DOSING_A" : "IDLE";
        }

        case AUTO_STATE_DOSING_A:
            return "MIXING_AFTER_A";

        case AUTO_STATE_MIXING_AFTER_A:
            return "DOSING_B";

        case AUTO_STATE_DOSING_B:
            return "COOLDOWN";

        case AUTO_STATE_WATER_FILL:
            return "IDLE";

        case AUTO_STATE_COOLDOWN:
            return "EVALUATING";

        default:
            return "UNKNOWN";
    }
}

int automatorGetTimeRemainingSec(void) {
    unsigned long now = millis();
    unsigned long elapsed = now - _stateStartTime;
    
    switch(_currentState) {
        case AUTO_STATE_DOSING_A:
            if (elapsed < AUTOMATOR_DOSE_A_MS) {
                return (AUTOMATOR_DOSE_A_MS - elapsed) / 1000;
            }
            break;
        case AUTO_STATE_MIXING_AFTER_A:
            if (elapsed < AUTOMATOR_MIX_AFTER_A_MS) {
                return (AUTOMATOR_MIX_AFTER_A_MS - elapsed) / 1000;
            }
            break;
        case AUTO_STATE_DOSING_B:
            if (elapsed < AUTOMATOR_DOSE_B_MS) {
                return (AUTOMATOR_DOSE_B_MS - elapsed) / 1000;
            }
            break;
        case AUTO_STATE_COOLDOWN:
            if (elapsed < AUTOMATOR_POST_DOSE_MIX_MS) {
                return (AUTOMATOR_POST_DOSE_MIX_MS - elapsed) / 1000;
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

    unsigned long now = millis();

    WaterSystemStatus waterStatus;
    waterSystemGetStatus(&waterStatus);

    const char* blockedReason = NULL;
    const char* blockedNextState = NULL;
    if (_automatorBlockedByWaterSystem(&waterStatus, &blockedReason, &blockedNextState)) {
        _stopAutomatorOutputs();
        if (!_blockedTimingHold) {
            _blockedTimingHold = true;
            _blockedTimingStart = now;
        }

        if (_currentState == AUTO_STATE_DISABLED || _currentState == AUTO_STATE_IDLE) {
            _updateReason(blockedReason ? blockedReason : "Blocked by control zone state");
        } else {
            _updateReason(blockedReason ? blockedReason : "Blocked by control zone state");
        }
        return;
    }

    if (_blockedTimingHold) {
        unsigned long blockedDuration = now - _blockedTimingStart;
        _stateStartTime += blockedDuration;
        _lastCheckTime += blockedDuration;
        _blockedTimingHold = false;
    }
    
    if (!_config.enabled) {
        if (_currentState != AUTO_STATE_DISABLED) {
            _changeState(AUTO_STATE_DISABLED, "Disabled");
        }
        return;
    }

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
            float tds = tdsGetLastValue();
            
            // Validate sensor read (don't act on NAN or 0)
            if (isnan(tds) || tds <= 5.0f) {
                _changeState(AUTO_STATE_IDLE, "Mix tank sensor data invalid. Waiting.");
                break;
            }

            // Current automation controls only the mix tank TDS path.
            if (_tdsNeedsDose(tds)) {
                char reason[128];
                snprintf(reason,
                         sizeof(reason),
                         "Mix tank TDS low (%.1f < %.1f - %.1f deadband) - Pumping A",
                         tds,
                         _config.targetTds,
                         _config.tdsHysteresisPpm);
                _changeState(AUTO_STATE_DOSING_A, reason);
                digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_ON);
            } 
            else {
                _changeState(AUTO_STATE_IDLE, "Mix tank parameters normal");
            }
            break;
        }

        case AUTO_STATE_DOSING_A:
            if (elapsed < _doseMsForVolume(_config.doseAVolumeMl)) {
                digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_ON);
            } else {
                _changeState(AUTO_STATE_MIXING_AFTER_A, "Nutrient A dosed. Mixing before Pump B");
            }
            break;

        case AUTO_STATE_MIXING_AFTER_A:
            if (elapsed >= _config.mixAfterAMs) {
                _changeState(AUTO_STATE_DOSING_B, "Pumping B after Nutrient A mix");
                digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_ON);
            }
            break;

        case AUTO_STATE_DOSING_B:
            if (elapsed < _doseMsForVolume(_config.doseBVolumeMl)) {
                digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_ON);
            } else {
                _changeState(AUTO_STATE_COOLDOWN, "Allowing A+B nutrients to mix before TDS recheck");
            }
            break;

        case AUTO_STATE_COOLDOWN:
            if (elapsed >= _config.postDoseMixMs) {
                _lastCheckTime = now;
                _changeState(AUTO_STATE_EVALUATING, "Post-dose mix finished. Rechecking TDS");
            }
            break;

        default:
            _changeState(AUTO_STATE_IDLE, "Reset via Unknown State");
            break;
    }
}
