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

static const unsigned long WATER_MIX_SETTLING_MS = 120000UL;

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

static void _writeRouteValve(WaterRefillRoute route) {
#if REFILL_ROUTE_VALVE_PIN >= 0
    digitalWrite(REFILL_ROUTE_VALVE_PIN,
                 route == WATER_REFILL_ROUTE_SUMP_DIRECT ? PUMP_ON : PUMP_OFF);
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
            return _hasFishTankRefillPump();
        case WATER_REFILL_ROUTE_SUMP_DIRECT:
            return _hasMixTankRefillSolenoid();
        default:
            return false;
    }
}

static WaterRefillRoute _resolveRefillRoute(void) {
    switch (_config.preferredRoute) {
        case WATER_REFILL_ROUTE_FISH_TANK:
            return _routeAvailable(WATER_REFILL_ROUTE_FISH_TANK) ? WATER_REFILL_ROUTE_FISH_TANK : WATER_REFILL_ROUTE_NONE;

        case WATER_REFILL_ROUTE_SUMP_DIRECT:
            return _routeAvailable(WATER_REFILL_ROUTE_SUMP_DIRECT) ? WATER_REFILL_ROUTE_SUMP_DIRECT : WATER_REFILL_ROUTE_NONE;

        case WATER_REFILL_ROUTE_AUTO:
            if (_config.allowDirectSumpRefill && _routeAvailable(WATER_REFILL_ROUTE_SUMP_DIRECT)) {
                return WATER_REFILL_ROUTE_SUMP_DIRECT;
            }
            if (_routeAvailable(WATER_REFILL_ROUTE_FISH_TANK)) {
                return WATER_REFILL_ROUTE_FISH_TANK;
            }
            if (_routeAvailable(WATER_REFILL_ROUTE_SUMP_DIRECT)) {
                return WATER_REFILL_ROUTE_SUMP_DIRECT;
            }
            return WATER_REFILL_ROUTE_NONE;

        case WATER_REFILL_ROUTE_NONE:
        default:
            return WATER_REFILL_ROUTE_NONE;
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
    _status.mixTankSettlingActive = settlingActive;
    _status.waterDilutionActive = _status.refillOutput || settlingActive;
    _status.mixTankControlZone = true;
    _status.dilutionHoldRemainingMs = dilutionHoldRemainingMs;
}

static void _sanitizeConfig(void) {
    if (_config.refillMaxRuntimeMs == 0 || _config.refillMaxRuntimeMs > WATER_REFILL_MAX_RUNTIME_MS) {
        _config.refillMaxRuntimeMs = WATER_REFILL_MAX_RUNTIME_MS;
    }

    if (_config.refillMinIntervalMs > WATER_FISH_REFILL_INTERVAL_MS) {
        _config.refillMinIntervalMs = WATER_FISH_REFILL_INTERVAL_MS;
    }

    if (_config.preferredRoute > WATER_REFILL_ROUTE_NONE) {
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

void waterSystemSetPreferredRoute(WaterRefillRoute route) {
    _config.preferredRoute = route;
    _sanitizeConfig();
    _saveConfig();
}

void waterSystemSetAllowDirectSumpRefill(bool enabled) {
    _config.allowDirectSumpRefill = enabled;
    _sanitizeConfig();
    _saveConfig();
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
}

void waterSystemGetStatus(WaterSystemStatus* status) {
    if (status) {
        *status = _status;
    }
}

void waterSystemLoop(void) {
    unsigned long now = millis();
    char stateReason[96];
    stateReason[0] = '\0';
    bool waitingForInterval = false;
    unsigned long mixIntervalRemainingMs = _getRemainingIntervalMs(now, _lastMixRefillStopMs, _config.refillMinIntervalMs);

    _status.hasCirculationPump = _hasCirculationPump();
    _status.hasRefillPump = _hasRefillPump();
    _status.hasLevelSensors = _hasLevelSensors();
    _status.hasOverflowSensor = _hasOverflowSensor();
    _status.hasRouteValve = _hasRouteValve();
    _status.levelLow = _readConfiguredInput(SUMP_LEVEL_LOW_PIN, WATER_LEVEL_TRIGGER_STATE);
    _status.levelHigh = _readConfiguredInput(SUMP_LEVEL_HIGH_PIN, WATER_LEVEL_TRIGGER_STATE);
    _status.overflowAlarm = _readConfiguredInput(FISH_TANK_OVERFLOW_PIN, OVERFLOW_SENSOR_TRIGGER_STATE);
    _status.routeBlocked = false;
    _status.activeRoute = WATER_REFILL_ROUTE_NONE;
    _status.routeValveOutput = false;
    _status.circulationPumpOutput = false;
    _status.fishTankRefillOutput = false;
    _status.mixTankRefillOutput = false;
    _status.waterDilutionActive = false;
    _status.mixTankSettlingActive = false;
    _status.mixTankControlZone = true;
    _status.dilutionHoldRemainingMs = 0;
    _status.fishRefillReady = true;
    _status.fishRefillWaitRemainingMs = 0;

    if (_status.hasLevelSensors && _status.levelLow && _status.levelHigh) {
        _alarmLatched = true;
        _setState(WATER_STATE_ALARM, "เซ็นเซอร์ระดับน้ำถังผสมขัดแย้งกัน");
    }

    bool circulationDesired = _config.circulationEnabled;
    bool refillDesired = false;
    bool blocked = false;
    WaterRefillRoute desiredRoute = WATER_REFILL_ROUTE_NONE;

    if (_config.manualRefill) {
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
        } else if (_status.levelLow) {
            if (mixIntervalRemainingMs > 0 && _lastMixRefillStopMs != 0) {
                waitingForInterval = true;
                snprintf(stateReason,
                         sizeof(stateReason),
                         "ถังผสมยังอยู่ในช่วงกันเติมถี่ (%lu วินาที)",
                         mixIntervalRemainingMs / 1000UL);
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

        desiredRoute = _resolveRefillRoute();

        if (desiredRoute == WATER_REFILL_ROUTE_NONE) {
            refillDesired = false;
            blocked = true;
            _status.routeBlocked = true;
            _setState(WATER_STATE_BLOCKED, "ไม่มีเส้นทางเติมที่ใช้งานได้ตาม config ปัจจุบัน");
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
        _refillStartMs = 0;
    }

    if (_status.refillOutput) {
        _lastActiveRefillRoute = _status.activeRoute;
    } else if (!_refillWasActive) {
        _lastActiveRefillRoute = WATER_REFILL_ROUTE_NONE;
    }
    _refillWasActive = _status.refillOutput;

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
                         ? "กำลังสั่งปั๊มเติมน้ำเข้าตู้ปลาแบบสั่งด้วยมือ"
                         : "กำลังเปิดโซลินอยด์เติมน้ำเข้าถังผสมแบบสั่งด้วยมือ");
        } else {
            snprintf(stateReason,
                     sizeof(stateReason),
                     _status.activeRoute == WATER_REFILL_ROUTE_FISH_TANK
                         ? "ถังผสมระดับต่ำ ระบบกำลังปั๊มน้ำจากถังสะอาดเข้าตู้ปลา"
                         : "ถังผสมระดับต่ำ ระบบกำลังเปิดโซลินอยด์น้ำเข้า");
        }
        _setState(_status.activeRoute == WATER_REFILL_ROUTE_FISH_TANK
                      ? WATER_STATE_FISH_TANK_REFILL
                      : WATER_STATE_MIX_TANK_REFILL,
                  stateReason);
    } else if (_status.mixTankSettlingActive) {
        snprintf(stateReason,
                 sizeof(stateReason),
                 "รอให้น้ำในถังผสมนิ่งหลัง%s (%lu วินาที)",
                 "เติมเข้าถังผสม",
                 _status.dilutionHoldRemainingMs / 1000UL);
        _setState(WATER_STATE_MIX_TANK_SETTLING, stateReason);
    } else if (_status.circulationOutput) {
        _setState(WATER_STATE_IDLE, "โซนถังผสมกำลังหมุนน้ำและพร้อมทำงาน");
    } else {
        _setState(WATER_STATE_IDLE, "ระบบน้ำหยุดอยู่");
    }
}