/**
 * @file lightController.cpp
 * @brief Implementation สำหรับ Light Controller
 */

#include "lightController.h"
#include "wifiConn.h"
#include <time.h>
#include <Adafruit_NeoPixel.h>

// NeoPixel สำหรับ RGB LED บน ESP32-S3
#define NEOPIXEL_PIN    48       // RGB LED on ESP32-S3-DevKitC
#define NEOPIXEL_COUNT  1        // 1 LED
static Adafruit_NeoPixel _neopixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static bool _lightEnabled = false;
static int _onDay = 0;       // 0=Sun, 1=Mon, ..., 6=Sat
static int _onHour = 0;
static int _onMinute = 0;
static int _offDay = 0;
static int _offHour = 0;
static int _offMinute = 0;
static bool _currentState = false;
static bool _ntpSynced = false;
static unsigned long _lastCheckTime = 0;

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief แปลงเวลา HH:MM เป็น hour และ minute
 */
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
 * @brief แปลง day+hour+minute เป็น "week minutes" (นาทีนับจากอาทิตย์ 00:00)
 * ช่วง 0 - 10079 (7 วัน * 24 ชม * 60 นาที)
 */
static int _toWeekMinutes(int day, int hour, int minute) {
    return day * 24 * 60 + hour * 60 + minute;
}

/**
 * @brief ตรวจสอบว่าเวลาปัจจุบันอยู่ในช่วง schedule หรือไม่ (รองรับข้ามหลายวัน)
 */
static bool _isInSchedule(int currentDay, int currentHour, int currentMinute) {
    int current = _toWeekMinutes(currentDay, currentHour, currentMinute);
    int on = _toWeekMinutes(_onDay, _onHour, _onMinute);
    int off = _toWeekMinutes(_offDay, _offHour, _offMinute);
    
    if (on <= off) {
        // ช่วงปกติ (เช่น จ.16:00 - พ.16:00)
        return (current >= on && current < off);
    } else {
        // ช่วงข้ามสัปดาห์ (เช่น ศ.16:00 - จ.16:00)
        return (current >= on || current < off);
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void lightCtrlSetup(void) {
    Serial.println(F("[LIGHT_CTRL] Initializing..."));
    
    // ตั้งค่า NeoPixel RGB LED
    _neopixel.begin();
    _neopixel.setBrightness(50);  // 0-255
    _neopixel.clear();
    _neopixel.show();
    _currentState = false;
    
    Serial.println(F("[LIGHT_CTRL] NeoPixel RGB LED initialized (GPIO 48)"));
    
    // ตั้งค่า NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.println(F("[LIGHT_CTRL] NTP configured"));
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
        if (showDebug) {
            Serial.println(F("[LIGHT_CTRL] System DISABLED (lightEnabled=0)"));
        }
        return;
    }
    
    // ถ้า WiFi ไม่ได้เชื่อมต่อ ใช้เวลาจาก RTC ภายใน
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        if (showDebug) {
            Serial.println(F("[LIGHT_CTRL] NTP NOT synced yet!"));
        }
        return;
    }
    
    if (!_ntpSynced) {
        _ntpSynced = true;
        Serial.println(F("[LIGHT_CTRL] NTP synced!"));
    }
    
    // DEBUG: แสดงเวลาปัจจุบัน
    static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (showDebug) {
        Serial.printf("[LIGHT_CTRL] Now: %s %02d:%02d | ON: %s %02d:%02d | OFF: %s %02d:%02d\n",
                      dayNames[timeinfo.tm_wday], timeinfo.tm_hour, timeinfo.tm_min,
                      dayNames[_onDay], _onHour, _onMinute,
                      dayNames[_offDay], _offHour, _offMinute);
    }
    
    // ตรวจสอบว่าเวลาปัจจุบันอยู่ใน schedule หรือไม่
    bool shouldBeOn = _isInSchedule(timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min);
    
    if (showDebug) {
        Serial.printf("[LIGHT_CTRL] shouldBeOn=%s, currentState=%s\n",
                      shouldBeOn ? "YES" : "NO", _currentState ? "ON" : "OFF");
    }
    
    if (shouldBeOn && !_currentState) {
        lightCtrlSetState(true);
        Serial.printf("[LIGHT_CTRL] Schedule ON at %s %02d:%02d\n", 
                      dayNames[timeinfo.tm_wday], timeinfo.tm_hour, timeinfo.tm_min);
    } else if (!shouldBeOn && _currentState) {
        lightCtrlSetState(false);
        Serial.printf("[LIGHT_CTRL] Schedule OFF at %s %02d:%02d\n", 
                      dayNames[timeinfo.tm_wday], timeinfo.tm_hour, timeinfo.tm_min);
    }
}

void lightCtrlSetState(bool state) {
    _currentState = state;
    
    // ใช้ NeoPixel RGB LED แทน digitalWrite
    if (state) {
        // ON: สีเขียว
        _neopixel.setPixelColor(0, _neopixel.Color(0, 255, 0));
    } else {
        // OFF: ปิด LED
        _neopixel.setPixelColor(0, _neopixel.Color(0, 0, 0));
    }
    _neopixel.show();
    
    Serial.printf("[LIGHT_CTRL] NeoPixel: %s\n", state ? "GREEN (ON)" : "OFF");
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
    if (!getLocalTime(&timeinfo)) {
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
    Serial.printf("[LIGHT_CTRL] Enabled: %s\n", _lightEnabled ? "YES" : "NO");
}

void lightCtrlSetOnDay(int day) {
    if (day >= 0 && day <= 6) {
        _onDay = day;
        static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        Serial.printf("[LIGHT_CTRL] ON Day: %s (%d)\n", dayNames[_onDay], _onDay);
    }
}

void lightCtrlSetOnTime(const char* onTime) {
    if (onTime && strlen(onTime) >= 4) {
        _parseTime(onTime, &_onHour, &_onMinute);
        Serial.printf("[LIGHT_CTRL] ON Time: %02d:%02d\n", _onHour, _onMinute);
    }
}

void lightCtrlSetOffDay(int day) {
    if (day >= 0 && day <= 6) {
        _offDay = day;
        static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        Serial.printf("[LIGHT_CTRL] OFF Day: %s (%d)\n", dayNames[_offDay], _offDay);
    }
}

void lightCtrlSetOffTime(const char* offTime) {
    if (offTime && strlen(offTime) >= 4) {
        _parseTime(offTime, &_offHour, &_offMinute);
        Serial.printf("[LIGHT_CTRL] OFF Time: %02d:%02d\n", _offHour, _offMinute);
    }
}

void lightCtrlPrintSchedule(void) {
    static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    Serial.println(F("[LIGHT_CTRL] === Current Schedule ==="));
    Serial.printf("  Enabled: %s\n", _lightEnabled ? "YES" : "NO");
    Serial.printf("  ON:  %s %02d:%02d\n", dayNames[_onDay], _onHour, _onMinute);
    Serial.printf("  OFF: %s %02d:%02d\n", dayNames[_offDay], _offHour, _offMinute);
    Serial.println(F("[LIGHT_CTRL] ======================="));
}

