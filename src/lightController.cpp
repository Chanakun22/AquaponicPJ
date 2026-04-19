/**
 * @file lightController.cpp
 * @brief Implementation สำหรับ Light Controller
 */

#include "lightController.h"
#include "logger.h"
#include "wifiConn.h"
#include <time.h>
#include <Preferences.h>
#include <string.h>

// Light Relay Control (Active LOW: LOW = ON, HIGH = OFF)

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static bool _lightEnabled = false;
static bool _manualState = false;
static CommandSource _commandSource = LIGHT_DEFAULT_COMMAND_SOURCE;
static int _onDay = 0;       // 0=Sun, 1=Mon, ..., 6=Sat, 7=Everyday
static int _onHour = 0;
static int _onMinute = 0;
static int _offDay = 0;      // 0=Sun, 1=Mon, ..., 6=Sat, 7=Everyday
static int _offHour = 0;
static int _offMinute = 0;
static bool _currentState = false;
static bool _ntpSynced = false;
static unsigned long _lastCheckTime = 0;
static Preferences _lightPrefs;
static LightControlStatus _status = {false, false, true, "Light controller not initialized"};

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

static void _parseTime(const char* timeStr, int* hour, int* minute) {
    if (timeStr && strlen(timeStr) >= 4) {
        *hour = atoi(timeStr);
        const char* colonPos = strchr(timeStr, ':');
        if (colonPos) {
            *minute = atoi(colonPos + 1);
        }
    }
}

/**
 * @brief บันทึกค่าตารางเวลาลง NVS Memory
 */
static void _saveSchedule(void) {
    _lightPrefs.begin("light_sched", false);
    _lightPrefs.putInt("source", (int)_commandSource);
    _lightPrefs.putBool("enabled", _lightEnabled);
    _lightPrefs.putBool("manual", _manualState);
    _lightPrefs.putInt("onDay", _onDay);
    _lightPrefs.putInt("onHour", _onHour);
    _lightPrefs.putInt("onMin", _onMinute);
    _lightPrefs.putInt("offDay", _offDay);
    _lightPrefs.putInt("offHour", _offHour);
    _lightPrefs.putInt("offMin", _offMinute);
    _lightPrefs.end();
    LOG_DEBUG("Light schedule saved to NVS");
}

/**
 * @brief โหลดค่าตารางเวลาจาก NVS Memory (เรียกตอน Boot)
 */
static void _loadSchedule(void) {
    _lightPrefs.begin("light_sched", true);
    _commandSource = (CommandSource)_lightPrefs.getInt("source", (int)LIGHT_DEFAULT_COMMAND_SOURCE);
    _lightEnabled = _lightPrefs.getBool("enabled", false);
    _manualState = _lightPrefs.getBool("manual", false);
    _onDay = _lightPrefs.getInt("onDay", 0);
    _onHour = _lightPrefs.getInt("onHour", 6);
    _onMinute = _lightPrefs.getInt("onMin", 0);
    _offDay = _lightPrefs.getInt("offDay", 0);
    _offHour = _lightPrefs.getInt("offHour", 18);
    _offMinute = _lightPrefs.getInt("offMin", 0);
    _lightPrefs.end();
    LOG_INFO("Loaded Light Schedule from NVS");
}

static void _setReason(const char* reason) {
    if (strncmp(_status.reason, reason, sizeof(_status.reason)) != 0) {
        snprintf(_status.reason, sizeof(_status.reason), "%s", reason);
        LOG_INFO("[LIGHT] %s", _status.reason);
    }
}

/**
 * @brief แปลง day+hour+minute เป็น "week minutes" (นาทีนับจากอาทิตย์ 00:00)
 * ช่วง 0 - 10079 (7 วัน * 24 ชม * 60 นาที)
 */
static int _toWeekMinutes(int day, int hour, int minute) {
    return day * 24 * 60 + hour * 60 + minute;
}

/**
 * @brief แปลง hour+minute เป็น "day minutes" (0-1439)
 */
static int _toDayMinutes(int hour, int minute) {
    return hour * 60 + minute;
}

/**
 * @brief ตรวจสอบว่าเวลาปัจจุบันอยู่ในช่วง schedule หรือไม่
 * รองรับ day=7 (Everyday) และข้ามหลายวัน
 */
static bool _isInSchedule(int currentDay, int currentHour, int currentMinute) {
    // Case 1: Both Everyday (day=7) - daily schedule, compare time only
    if (_onDay == 7 && _offDay == 7) {
        int currentMin = _toDayMinutes(currentHour, currentMinute);
        int onMin = _toDayMinutes(_onHour, _onMinute);
        int offMin = _toDayMinutes(_offHour, _offMinute);
        
        if (onMin <= offMin) {
            // Normal: e.g. 06:00 - 18:00
            return (currentMin >= onMin && currentMin < offMin);
        } else {
            // Overnight: e.g. 22:00 - 06:00
            return (currentMin >= onMin || currentMin < offMin);
        }
    }
    
    // Case 2: ON=Everyday, OFF=specific day
    if (_onDay == 7) {
        int currentMin = _toDayMinutes(currentHour, currentMinute);
        int onMin = _toDayMinutes(_onHour, _onMinute);
        
        // ON every day at onTime, OFF only on specific day
        if (currentDay == _offDay && currentMin >= _toDayMinutes(_offHour, _offMinute)) {
            return false;  // Past OFF time on OFF day
        }
        return (currentMin >= onMin);
    }
    
    // Case 3: ON=specific day, OFF=Everyday
    if (_offDay == 7) {
        int currentMin = _toDayMinutes(currentHour, currentMinute);
        int offMin = _toDayMinutes(_offHour, _offMinute);
        int currentWeek = _toWeekMinutes(currentDay, currentHour, currentMinute);
        int onWeek = _toWeekMinutes(_onDay, _onHour, _onMinute);
        
        // OFF every day at offTime, ON only on specific day
        if (currentMin >= offMin) {
            return false;  // Past OFF time today
        }
        return (currentWeek >= onWeek);
    }
    
    // Case 4: Both specific days - original week-based logic
    int current = _toWeekMinutes(currentDay, currentHour, currentMinute);
    int on = _toWeekMinutes(_onDay, _onHour, _onMinute);
    int off = _toWeekMinutes(_offDay, _offHour, _offMinute);
    
    if (on <= off) {
        // Normal range (e.g. Mon 16:00 - Wed 16:00)
        return (current >= on && current < off);
    } else {
        // Wrap around week (e.g. Fri 16:00 - Mon 16:00)
        return (current >= on || current < off);
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void lightCtrlSetup(void) {
    LOG_INFO("Initializing light controller...");
    
    // ตั้งค่า Light Relay
    pinMode(LIGHT_RELAY_PIN, OUTPUT);
    digitalWrite(LIGHT_RELAY_PIN, HIGH);  // OFF (Active LOW)
    _currentState = false;
    _status.hasOutput = true;
    
    LOG_INFO("Light Relay initialized (GPIO %d)", LIGHT_RELAY_PIN);
    
    // โหลดตารางเวลาจาก Memory (Offline mode support)
    _loadSchedule();
    lightCtrlPrintSchedule(); // แสดงค่าที่โหลดมา
    
    // ตั้งค่า NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    LOG_INFO("NTP configured");
}

void lightCtrlLoop(void) {
    // ตรวจสอบเวลา (Non-blocking)
    if (millis() - _lastCheckTime < LIGHT_CHECK_INTERVAL) {
        return;
    }
    _lastCheckTime = millis();
    
    // DEBUG: แสดงสถานะทุก 10 วินาที
    static unsigned long _lastDebugTime = 0;
    bool showDebug = (millis() - _lastDebugTime >= 10000);
    if (showDebug) {
        _lastDebugTime = millis();
    }
    
    // ถ้ายังไม่ได้เปิดใช้งาน ไม่ต้องทำอะไร
    if (!_lightEnabled) {
        if (_currentState != _manualState) {
            lightCtrlSetState(_manualState);
        }
        _status.running = _currentState;
        _status.ntpSynced = _ntpSynced;
        _setReason(_manualState ? "Manual ON" : "Manual OFF");
        if (showDebug) {
            LOG_DEBUG("Light controller DISABLED (lightEnabled=0)");
        }
        return;
    }
    
    // ถ้า WiFi ไม่ได้เชื่อมต่อ ใช้เวลาจาก RTC ภายใน
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) {  // 10ms timeout — non-blocking
        _status.running = _currentState;
        _status.ntpSynced = false;
        _setReason("Waiting for NTP time sync");
        if (showDebug) {
            LOG_DEBUG("NTP NOT synced yet!");
        }
        return;
    }
    
    if (!_ntpSynced) {
        _ntpSynced = true;
        LOG_INFO("NTP synced!");
    }
    _status.ntpSynced = true;
    
    // DEBUG: แสดงเวลาปัจจุบัน
    static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Every"};
    if (showDebug) {
        LOG_DEBUG("Now: %s %02d:%02d | ON: %s %02d:%02d | OFF: %s %02d:%02d",
                  dayNames[timeinfo.tm_wday], timeinfo.tm_hour, timeinfo.tm_min,
                  (_onDay <= 7) ? dayNames[_onDay] : "?", _onHour, _onMinute,
                  (_offDay <= 7) ? dayNames[_offDay] : "?", _offHour, _offMinute);
    }
    
    // ตรวจสอบว่าเวลาปัจจุบันอยู่ใน schedule หรือไม่
    bool shouldBeOn = _isInSchedule(timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min);
    
    if (showDebug) {
        LOG_DEBUG("shouldBeOn=%s, currentState=%s",
                  shouldBeOn ? "YES" : "NO", _currentState ? "ON" : "OFF");
    }
    
    if (shouldBeOn && !_currentState) {
        lightCtrlSetState(true);
        LOG_INFO("Schedule ON at %s %02d:%02d", 
                 dayNames[timeinfo.tm_wday], timeinfo.tm_hour, timeinfo.tm_min);
    } else if (!shouldBeOn && _currentState) {
        lightCtrlSetState(false);
        LOG_INFO("Schedule OFF at %s %02d:%02d", 
                 dayNames[timeinfo.tm_wday], timeinfo.tm_hour, timeinfo.tm_min);
    }
    _status.running = _currentState;
    _setReason(_currentState ? "Schedule keeps light ON" : "Schedule keeps light OFF");
}

void lightCtrlSetState(bool state) {
    _currentState = state;
    _status.running = state;
    
    // Relay control (Active LOW: LOW = ON, HIGH = OFF)
    if (state) {
        digitalWrite(LIGHT_RELAY_PIN, LOW);   // ON
    } else {
        digitalWrite(LIGHT_RELAY_PIN, HIGH);  // OFF
    }
    
    LOG_DEBUG("Light Relay (GPIO %d): %s", LIGHT_RELAY_PIN, state ? "ON" : "OFF");
}

bool lightCtrlGetState(void) {
    return _currentState;
}

bool lightCtrlIsEnabled(void) {
    return _lightEnabled;
}

bool lightCtrlGetTime(char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize < 6) {
        return false;
    }
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) {  // 10ms timeout — non-blocking
        strncpy(buffer, "N/A", bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        return false;
    }
    
    snprintf(buffer, bufferSize, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return true;
}

// ============================================================================
// PARTIAL UPDATE FUNCTIONS
// ============================================================================

void lightCtrlSetEnabled(int enabled) {
    _lightEnabled = (enabled == 1);
    LOG_INFO("Light controller enabled: %s", _lightEnabled ? "YES" : "NO");
    if (!_lightEnabled) {
        lightCtrlSetState(_manualState);
    }
    _saveSchedule();
}

void lightCtrlSetManualState(bool state) {
    _manualState = state;
    if (!_lightEnabled) {
        lightCtrlSetState(state);
    }
    _saveSchedule();
}

void lightCtrlSetCommandSource(CommandSource source) {
    _commandSource = source;
    _saveSchedule();
    _setReason(source == COMMAND_SOURCE_NETPIE ? "Control source set to NETPIE" : "Control source set to Local Web");
}

void lightCtrlSetOnDay(int day) {
    if (day >= 0 && day <= 7) {
        _onDay = day;
        static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Everyday"};
        LOG_INFO("Light ON Day: %s (%d)", dayNames[_onDay], _onDay);
        _saveSchedule();
    }
}

void lightCtrlSetOnTime(const char* onTime) {
    if (onTime && strlen(onTime) >= 4) {
        _parseTime(onTime, &_onHour, &_onMinute);
        LOG_INFO("Light ON Time: %02d:%02d", _onHour, _onMinute);
        _saveSchedule();
    }
}

void lightCtrlSetOffDay(int day) {
    if (day >= 0 && day <= 7) {
        _offDay = day;
        static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Everyday"};
        LOG_INFO("Light OFF Day: %s (%d)", dayNames[_offDay], _offDay);
        _saveSchedule();
    }
}

void lightCtrlSetOffTime(const char* offTime) {
    if (offTime && strlen(offTime) >= 4) {
        _parseTime(offTime, &_offHour, &_offMinute);
        LOG_INFO("Light OFF Time: %02d:%02d", _offHour, _offMinute);
        _saveSchedule();
    }
}

void lightCtrlPrintSchedule(void) {
    static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Everyday"};
    LOG_INFO("=== Current Light Schedule ===");
    LOG_INFO("  Source : %s", lightCtrlGetCommandSourceString(_commandSource));
    LOG_INFO("  Enabled: %s", _lightEnabled ? "YES" : "NO");
    LOG_INFO("  Manual : %s", _manualState ? "ON" : "OFF");
    LOG_INFO("  ON:  %s %02d:%02d", (_onDay <= 7) ? dayNames[_onDay] : "?", _onHour, _onMinute);
    LOG_INFO("  OFF: %s %02d:%02d", (_offDay <= 7) ? dayNames[_offDay] : "?", _offHour, _offMinute);
    LOG_INFO("==============================");
}

CommandSource lightCtrlGetCommandSource(void) { return _commandSource; }

const char* lightCtrlGetCommandSourceString(CommandSource source) {
    return commandSourceToString(source);
}

bool lightCtrlAllowsNetpieControl(void) {
    return _commandSource == COMMAND_SOURCE_NETPIE;
}

bool lightCtrlAllowsLocalControl(void) {
    return _commandSource == COMMAND_SOURCE_LOCAL_WEB;
}

void lightCtrlGetConfig(LightControlConfig* config) {
    if (config == NULL) {
        return;
    }
    config->commandSource = _commandSource;
    config->enabled = _lightEnabled;
    config->manualState = _manualState;
    config->onDay = _onDay;
    config->offDay = _offDay;
    snprintf(config->onTime, sizeof(config->onTime), "%02d:%02d", _onHour, _onMinute);
    snprintf(config->offTime, sizeof(config->offTime), "%02d:%02d", _offHour, _offMinute);
}

void lightCtrlGetStatus(LightControlStatus* status) {
    if (status != NULL) {
        *status = _status;
    }
}

int lightCtrlGetOnDay(void) { return _onDay; }
int lightCtrlGetOffDay(void) { return _offDay; }

static char _onTimeBuff[6];
const char* lightCtrlGetOnTime(void) {
    snprintf(_onTimeBuff, sizeof(_onTimeBuff), "%02d:%02d", _onHour, _onMinute);
    return _onTimeBuff;
}

static char _offTimeBuff[6];
const char* lightCtrlGetOffTime(void) {
    snprintf(_offTimeBuff, sizeof(_offTimeBuff), "%02d:%02d", _offHour, _offMinute);
    return _offTimeBuff;
}

