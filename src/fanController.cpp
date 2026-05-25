/**
 * @file fanController.cpp
 * @brief Exhaust fan controller implementation
 */

#include "fanController.h"
#include "config.h"
#include "logger.h"
#include "dhtSensor.h"
#include "gpioOut.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>

static Preferences _fanPrefs;

static FanControlConfig _config = {
    FAN_DEFAULT_ENABLED,
    FAN_DEFAULT_AUTO_MODE,
    FAN_DEFAULT_MANUAL_STATE,
    FAN_DEFAULT_TEMP_ON_C,
    FAN_DEFAULT_TEMP_OFF_C,
    FAN_DEFAULT_HUMIDITY_ON_PCT,
    FAN_DEFAULT_HUMIDITY_OFF_PCT
};

static FanControlStatus _status = {
    FAN_STATE_DISABLED,
    false,
    false,
    NAN,
    NAN,
    "Fan controller not initialized"
};

static unsigned long _lastLoopMs = 0;

static bool _hasOutput(void) {
#if EXHAUST_FAN_PIN >= 0
    return true;
#else
    return false;
#endif
}

static void _writeOutput(bool enabled) {
#if EXHAUST_FAN_PIN >= 0
    gpioOutWrite(GPIO_OUT_EXHAUST_FAN, enabled);
#else
    (void)enabled;
#endif
    _status.running = enabled;
}

static void _setReason(const char* reason) {
    if (strncmp(_status.reason, reason, sizeof(_status.reason)) != 0) {
        snprintf(_status.reason, sizeof(_status.reason), "%s", reason);
        LOG_INFO("[FAN] %s", _status.reason);
    }
}

static void _setState(FanControlState state, const char* reason) {
    if (_status.state != state) {
        _status.state = state;
        LOG_INFO("[FAN] State -> %s", fanCtrlGetStateString(state));
    }
    _setReason(reason);
}

static void _saveConfig(void) {
    _fanPrefs.begin("fanCtrl", false);
    _fanPrefs.putBool("enabled", _config.enabled);
    _fanPrefs.putBool("autoMode", _config.autoMode);
    _fanPrefs.putBool("manual", _config.manualState);
    _fanPrefs.putFloat("tempOn", _config.tempOnC);
    _fanPrefs.putFloat("tempOff", _config.tempOffC);
    _fanPrefs.putFloat("humOn", _config.humidityOnPct);
    _fanPrefs.putFloat("humOff", _config.humidityOffPct);
    _fanPrefs.end();
}

static void _sanitizeConfig(void) {
    _config.tempOnC = constrain(_config.tempOnC, -20.0f, 80.0f);
    _config.tempOffC = constrain(_config.tempOffC, -20.0f, 80.0f);
    _config.humidityOnPct = constrain(_config.humidityOnPct, 0.0f, 100.0f);
    _config.humidityOffPct = constrain(_config.humidityOffPct, 0.0f, 100.0f);

    if (_config.tempOffC >= _config.tempOnC) {
        _config.tempOffC = _config.tempOnC - 1.0f;
    }
    if (_config.humidityOffPct >= _config.humidityOnPct) {
        _config.humidityOffPct = _config.humidityOnPct - 5.0f;
    }

    _config.tempOffC = constrain(_config.tempOffC, -20.0f, _config.tempOnC - 0.5f);
    _config.humidityOffPct = constrain(_config.humidityOffPct, 0.0f, _config.humidityOnPct - 1.0f);
}

static void _evaluateController(void) {
    _status.hasOutput = _hasOutput();
    _status.airTempC = dhtReadTemperature();
    _status.humidityPct = dhtReadHumidity();

    if (!_status.hasOutput) {
        _writeOutput(false);
        _setState(FAN_STATE_BLOCKED, "Set EXHAUST_FAN_PIN when fan relay wiring is finalized");
        return;
    }

    if (!_config.enabled) {
        _writeOutput(false);
        _setState(FAN_STATE_DISABLED, "Fan controller disabled");
        return;
    }

    if (!_config.autoMode) {
        _writeOutput(_config.manualState);
        _setState(_config.manualState ? FAN_STATE_RUNNING : FAN_STATE_IDLE,
                  _config.manualState ? "Manual ON" : "Manual OFF");
        return;
    }

    if (isnan(_status.airTempC) && isnan(_status.humidityPct)) {
        _writeOutput(false);
        _setState(FAN_STATE_BLOCKED, "Waiting for DHT air sensor data");
        return;
    }

    bool overTemp = !isnan(_status.airTempC) && _status.airTempC >= _config.tempOnC;
    bool overHumidity = !isnan(_status.humidityPct) && _status.humidityPct >= _config.humidityOnPct;
    bool coolEnough = isnan(_status.airTempC) || _status.airTempC <= _config.tempOffC;
    bool dryEnough = isnan(_status.humidityPct) || _status.humidityPct <= _config.humidityOffPct;

    bool shouldRun = _status.running;
    if (_status.running) {
        shouldRun = !(coolEnough && dryEnough);
    } else {
        shouldRun = overTemp || overHumidity;
    }

    _writeOutput(shouldRun);

    if (shouldRun) {
        if (overTemp && overHumidity) {
            _setState(FAN_STATE_RUNNING, "Auto ON by air temperature and humidity");
        } else if (overTemp) {
            _setState(FAN_STATE_RUNNING, "Auto ON by air temperature");
        } else if (overHumidity) {
            _setState(FAN_STATE_RUNNING, "Auto ON by humidity");
        } else {
            _setState(FAN_STATE_RUNNING, "Holding ON until temperature and humidity drop");
        }
    } else {
        _setState(FAN_STATE_IDLE, "Auto idle: air conditions within range");
    }
}

const char* fanCtrlGetStateString(FanControlState state) {
    switch (state) {
        case FAN_STATE_DISABLED: return "DISABLED";
        case FAN_STATE_IDLE: return "IDLE";
        case FAN_STATE_RUNNING: return "RUNNING";
        case FAN_STATE_BLOCKED: return "BLOCKED";
        default: return "UNKNOWN";
    }
}

const char* fanCtrlGetModeString(bool autoMode) {
    return autoMode ? "AUTO" : "MANUAL";
}

void fanCtrlSetup(void) {
    _fanPrefs.begin("fanCtrl", true);
    _config.enabled = _fanPrefs.getBool("enabled", FAN_DEFAULT_ENABLED);
    _config.autoMode = _fanPrefs.getBool("autoMode", FAN_DEFAULT_AUTO_MODE);
    _config.manualState = _fanPrefs.getBool("manual", FAN_DEFAULT_MANUAL_STATE);
    _config.tempOnC = _fanPrefs.getFloat("tempOn", FAN_DEFAULT_TEMP_ON_C);
    _config.tempOffC = _fanPrefs.getFloat("tempOff", FAN_DEFAULT_TEMP_OFF_C);
    _config.humidityOnPct = _fanPrefs.getFloat("humOn", FAN_DEFAULT_HUMIDITY_ON_PCT);
    _config.humidityOffPct = _fanPrefs.getFloat("humOff", FAN_DEFAULT_HUMIDITY_OFF_PCT);
    _fanPrefs.end();

    _sanitizeConfig();

#if EXHAUST_FAN_PIN >= 0
    // Output initialized by gpioOutSetup() in main.cpp; ensure stopped state.
    gpioOutWrite(GPIO_OUT_EXHAUST_FAN, false);
#endif

    _evaluateController();
}

void fanCtrlLoop(void) {
    if (millis() - _lastLoopMs < FAN_CONTROL_INTERVAL_MS) {
        return;
    }
    _lastLoopMs = millis();
    _evaluateController();
}

void fanCtrlSetConfig(bool enabled,
                      bool autoMode,
                      bool manualState,
                      float tempOnC,
                      float tempOffC,
                      float humidityOnPct,
                      float humidityOffPct) {
    _config.enabled = enabled;
    _config.autoMode = autoMode;
    _config.manualState = manualState;
    _config.tempOnC = tempOnC;
    _config.tempOffC = tempOffC;
    _config.humidityOnPct = humidityOnPct;
    _config.humidityOffPct = humidityOffPct;
    _sanitizeConfig();
    _saveConfig();
    _evaluateController();
}

void fanCtrlGetConfig(FanControlConfig* config) {
    if (config != NULL) {
        *config = _config;
    }
}

void fanCtrlGetStatus(FanControlStatus* status) {
    if (status != NULL) {
        *status = _status;
    }
}

void fanCtrlSetEnabled(bool enabled) {
    _config.enabled = enabled;
    _saveConfig();
    _evaluateController();
}

void fanCtrlSetAutoMode(bool enabled) {
    _config.autoMode = enabled;
    if (enabled) {
        _config.manualState = false;
    }
    _saveConfig();
    _evaluateController();
}

void fanCtrlSetManualState(bool enabled) {
    _config.autoMode = false;
    _config.manualState = enabled;
    _saveConfig();
    _evaluateController();
}

bool fanCtrlGetState(void) {
    return _status.running;
}