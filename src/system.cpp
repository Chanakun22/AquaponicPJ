/**
 * @file system.cpp
 * @brief System Management Implementation
 */

#include "system.h"
#include "logger.h"
#include "config.h"
#include "phSensor.h"
#include "TdsSensor.h"
#include "tempSensor.h"
#include "dhtSensor.h"
#include "lightSensor.h"
#include <Preferences.h>
#include <WiFi.h>

#if defined(ESP32)
#include <ESP.h>

#define RTC_CRASH_SNAPSHOT_MAGIC 0x43524153UL
RTC_NOINIT_ATTR static unsigned long _rtcTaskHeartbeat[TASK_ID_COUNT];
RTC_NOINIT_ATTR static char _rtcTaskProgress[TASK_ID_COUNT][32];
RTC_NOINIT_ATTR static unsigned long _rtcCrashSnapshotMagic;
#endif

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static unsigned long _bootTime = 0;
static unsigned long _minFreeHeap = UINT32_MAX;
static unsigned int _watchdogResets = 0;
static unsigned int _wifiReconnects = 0;
static unsigned int _mqttReconnects = 0;
static bool _systemInitialized = false;

static Preferences _prefs;
static char _lastCrashInfoCache[160] = "";
static bool _hasLastCrashInfoCache = false;

#if defined(ESP32)
static void _clearRtcCrashSnapshot(void) {
    memset(_rtcTaskHeartbeat, 0, sizeof(_rtcTaskHeartbeat));
    memset(_rtcTaskProgress, 0, sizeof(_rtcTaskProgress));
    _rtcCrashSnapshotMagic = 0;
}

static void _captureRtcCrashSnapshotFallback(void) {
    if (_rtcCrashSnapshotMagic != RTC_CRASH_SNAPSHOT_MAGIC) {
        return;
    }

    int oldestTask = -1;
    unsigned long oldestHeartbeat = ULONG_MAX;

    for (int i = 0; i < TASK_ID_COUNT; i++) {
        if (_rtcTaskHeartbeat[i] == 0) {
            continue;
        }

        if (_rtcTaskHeartbeat[i] < oldestHeartbeat) {
            oldestHeartbeat = _rtcTaskHeartbeat[i];
            oldestTask = i;
        }
    }

    if (oldestTask >= 0) {
        const char* stage = _rtcTaskProgress[oldestTask][0] != '\0' ? _rtcTaskProgress[oldestTask] : "unknown";
        snprintf(_lastCrashInfoCache, sizeof(_lastCrashInfoCache),
                 "WDT fallback: oldest task '%s' last checkpoint at %lus (stage=%s)",
                 TASK_NAMES[oldestTask], oldestHeartbeat / 1000, stage);
        _hasLastCrashInfoCache = true;
    }

    _clearRtcCrashSnapshot();
}
#endif

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

static void _loadPersistedStats(void) {
    _prefs.begin("system", true);  // Read-only
    
    _watchdogResets = _prefs.getUInt("wdResets", 0);
    _wifiReconnects = _prefs.getUInt("wifiReconnects", 0);
    _mqttReconnects = _prefs.getUInt("mqttReconnects", 0);
    
    _prefs.end();
    
    LOG_DEBUG("Loaded persisted stats: WD=%u, WiFi=%u, MQTT=%u", 
              _watchdogResets, _wifiReconnects, _mqttReconnects);
}

static void _savePersistedStats(void) {
    _prefs.begin("system", false);
    
    _prefs.putUInt("wdResets", _watchdogResets);
    _prefs.putUInt("wifiReconnects", _wifiReconnects);
    _prefs.putUInt("mqttReconnects", _mqttReconnects);
    
    _prefs.end();
}

#if defined(ESP32)
static bool _isWatchdogResetReason(esp_reset_reason_t reason) {
    return reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT;
}
#endif

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void systemInit(void) {
    if (_systemInitialized) {
        return;
    }
    
    _bootTime = millis();
    
    // Load persisted statistics
    _loadPersistedStats();

    #if defined(ESP32)
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (_isWatchdogResetReason(resetReason)) {
        _watchdogResets++;
        _savePersistedStats();
        LOG_WARN("Previous boot ended with watchdog reset (%s)", systemGetResetReasonString());

        if (!_hasLastCrashInfoCache) {
            _captureRtcCrashSnapshotFallback();
        }
    } else {
        _clearRtcCrashSnapshot();
    }
    #endif
    
    // Load Sensor States
    systemSensorInit();
    
    // Get initial heap
    #if defined(ESP32)
    _minFreeHeap = ESP.getFreeHeap();
    #else
    _minFreeHeap = 0;
    #endif

    // Initialize Factory Reset Pin
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    
    _systemInitialized = true;
    
    LOG_INFO("System initialized - Version %s", FIRMWARE_VERSION_STRING);
    #if defined(ESP32)
    LOG_INFO("Free heap: %lu bytes", ESP.getFreeHeap());
    #endif
}

void systemLoop(void) {
    if (!_systemInitialized) {
        return;
    }

    // Factory Reset Button Logic
    static unsigned long _btnPressStart = 0;
    if (digitalRead(FACTORY_RESET_PIN) == LOW) {
        if (_btnPressStart == 0) {
            _btnPressStart = millis();
        } else if (millis() - _btnPressStart > FACTORY_RESET_TIME) {
            // Button held long enough
            LOG_WARN("Factory Reset Button Detected!");
            
            // Blink LED rapidly to indicate reset
            // Note: Removed LED blinking to avoid GPIO conflict
            LOG_INFO("Resetting in 3... 2... 1...");
            LOG_INFO("Resetting in 3... 2... 1...");
            // Non-blocking wait not strictly needed here since we are about to reset anyway, 
            // but removing delay avoids WDT risk if it was tight.
            
            
            systemFactoryReset();
            _btnPressStart = 0; // Reset counter (though system will reboot)
        }
    } else {
        _btnPressStart = 0; // Button released
    }
    
    // Update minimum free heap
    #if defined(ESP32)
    unsigned long currentFreeHeap = ESP.getFreeHeap();
    if (currentFreeHeap < _minFreeHeap) {
        _minFreeHeap = currentFreeHeap;
    }
    #endif
    
    // Save stats periodically (every 5 minutes)
    static unsigned long lastSaveTime = 0;
    if (millis() - lastSaveTime >= 300000) {
        lastSaveTime = millis();
        _savePersistedStats();
    }
}

void systemGetHealth(SystemHealth_t* health) {
    if (!health) {
        return;
    }
    
    health->uptimeMs = millis() - _bootTime;
    
    #if defined(ESP32)
    health->freeHeap = ESP.getFreeHeap();
    health->heapSize = ESP.getHeapSize();
    health->minFreeHeap = _minFreeHeap;
    health->cpuTemp = temperatureRead();
    #else
    health->freeHeap = 0;
    health->heapSize = 0;
    health->minFreeHeap = 0;
    health->cpuTemp = 0.0f;
    #endif
    
    health->watchdogResets = _watchdogResets;
    health->wifiReconnects = _wifiReconnects;
    health->mqttReconnects = _mqttReconnects;
    const char* rst = systemGetResetReasonString();
    strlcpy(health->resetReason, rst, sizeof(health->resetReason));
    
    // Check if sensors are OK
    bool sensorsOk = true;
    
    // Check pH Sensor
    if (systemGetSensorEnabled(SENSOR_PH)) {
        if (!phIsReady() || isnan(phRead())) {
            sensorsOk = false;
            LOG_DEBUG("Health: pH sensor not ready or NAN");
        }
    }
    
    // Check TDS Sensor
    if (systemGetSensorEnabled(SENSOR_TDS)) {
        if (!tdsIsReady() || tdsGetLastValue() < 0) {
            sensorsOk = false;
            LOG_DEBUG("Health: TDS sensor not ready or invalid");
        }
    }
    
    // Check Water Temp (DS18B20)
    if (systemGetSensorEnabled(SENSOR_WATER_TEMP)) {
        if (isnan(tempRead())) {
            sensorsOk = false;
            LOG_DEBUG("Health: Water temp sensor NAN");
        }
    }
    
    // Check Air Temp/Humidity (DHT22)
    if (systemGetSensorEnabled(SENSOR_AIR_TEMP)) {
        if (isnan(dhtReadTemperature()) || isnan(dhtReadHumidity())) {
            sensorsOk = false;
            LOG_DEBUG("Health: DHT22 sensor NAN");
        }
    }
    
    // Check Light Sensor (BH1750)
    if (systemGetSensorEnabled(SENSOR_LIGHT)) {
        if (!lightIsReady() || lightRead() < 0) {
            sensorsOk = false;
            LOG_DEBUG("Health: Light sensor not ready or invalid");
        }
    }
    
    health->sensorsOk = sensorsOk;
}

void systemFactoryReset(void) {
    LOG_WARN("Factory reset initiated!");
    
    // Clear all preferences
    Preferences prefs;

    // Force erase WiFi credentials from NVS
    #if defined(ESP32)
    WiFi.disconnect(true, true);  // Turn off and erase credentials
    #endif
    
    // Clear pH calibration
    prefs.begin("phSensor", false);
    prefs.clear();
    prefs.end();
    
    // Clear TDS calibration
    prefs.begin("tdsSensor", false);
    prefs.clear();
    prefs.end();
    
    // Clear system stats
    prefs.begin("system", false);
    prefs.clear();
    prefs.end();
    
    LOG_INFO("All settings cleared. Restarting...");
    
    // ใช้ vTaskDelay แทน delay() เพื่อหลีกเลี่ยง Watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
}

const char* systemGetVersion(void) {
    return FIRMWARE_VERSION_STRING;
}

unsigned long systemGetUptimeSeconds(void) {
    return (millis() - _bootTime) / 1000;
}

bool systemIsHealthy(void) {
    #if defined(ESP32)
    unsigned long freeHeap = ESP.getFreeHeap();
    
    // System unhealthy if free heap < 20KB
    if (freeHeap < 20480) {
        return false;
    }
    #endif
    
    return true;
}

void systemIncrementWatchdogResets(void) {
    _watchdogResets++;
    _savePersistedStats();
}

void systemIncrementWifiReconnects(void) {
    _wifiReconnects++;
    _savePersistedStats();
}

void systemIncrementMqttReconnects(void) {
    _mqttReconnects++;
    _savePersistedStats();
}

const char* systemGetResetReasonString(void) {
    #if defined(ESP32)
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "Unknown";
        case ESP_RST_POWERON:   return "Power On";
        case ESP_RST_EXT:       return "External Pin";
        case ESP_RST_SW:        return "Software Reset";
        case ESP_RST_PANIC:     return "Panic/Exception";
        case ESP_RST_INT_WDT:   return "Interrupt WDT";
        case ESP_RST_TASK_WDT:  return "Task WDT";
        case ESP_RST_WDT:       return "Other WDT";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "Other";
    }
    #else
    return "Unknown";  // Non-ESP32 platform
    #endif
}

// ============================================================================
// SENSOR MANAGEMENT IMPLEMENTATION
// ============================================================================

static bool _sensorEnabled[SENSOR_COUNT] = { true, true, true, true, true };
static const char* _sensorKeys[SENSOR_COUNT] = { "sns_tds", "sns_ph", "sns_water", "sns_air", "sns_light" };

void systemSensorInit(void) {
    _prefs.begin("system", true); // Read-only
    
    for (int i = 0; i < SENSOR_COUNT; i++) {
        _sensorEnabled[i] = _prefs.getBool(_sensorKeys[i], true);
        LOG_INFO("Sensor [%d] %s: %s", i, _sensorKeys[i], _sensorEnabled[i] ? "ENABLED" : "DISABLED");
    }
    
    _prefs.end();
}

void systemSetSensorEnabled(SensorId_t id, bool enabled) {
    if (id < 0 || id >= SENSOR_COUNT) return;
    
    if (_sensorEnabled[id] != enabled) {
        _sensorEnabled[id] = enabled;
        
        _prefs.begin("system", false); // Read-write
        _prefs.putBool(_sensorKeys[id], enabled);
        _prefs.end();
        
        LOG_INFO("Set Sensor [%d] to %s", id, enabled ? "ENABLED" : "DISABLED");
    }
}

void systemSetAllSensorsEnabled(bool states[SENSOR_COUNT]) {
    // Open NVS once for all writes (prevents race condition)
    _prefs.begin("system", false);
    
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (_sensorEnabled[i] != states[i]) {
            _sensorEnabled[i] = states[i];
            _prefs.putBool(_sensorKeys[i], states[i]);
            LOG_INFO("Set Sensor [%d] to %s", i, states[i] ? "ENABLED" : "DISABLED");
        }
    }
    
    _prefs.end();
    LOG_INFO("All sensor states saved to NVS");
}

bool systemGetSensorEnabled(SensorId_t id) {
    if (id < 0 || id >= SENSOR_COUNT) return false;
    return _sensorEnabled[id];
}

// ============================================================================
// TASK HEARTBEAT MONITOR IMPLEMENTATION
// ============================================================================

const char* TASK_NAMES[TASK_ID_COUNT] = {
    "Networking",
    "Sensors",
    "Control"
};

static volatile unsigned long _taskHeartbeat[TASK_ID_COUNT] = {0, 0, 0};
static TaskHandle_t _taskHandles[TASK_ID_COUNT] = {NULL, NULL, NULL};
static char _taskProgress[TASK_ID_COUNT][32] = {
    "startup",
    "startup",
    "startup"
};
static bool _taskStuckLatched[TASK_ID_COUNT] = {false, false, false};

void systemTaskHeartbeat(TaskId_t taskId) {
    if (taskId >= 0 && taskId < TASK_ID_COUNT) {
        unsigned long now = millis();
        _taskHeartbeat[taskId] = now;

        #if defined(ESP32)
        _rtcTaskHeartbeat[taskId] = now;
        _rtcCrashSnapshotMagic = RTC_CRASH_SNAPSHOT_MAGIC;
        #endif
    }
}

void systemSetTaskProgress(TaskId_t taskId, const char* stage) {
    if (taskId < 0 || taskId >= TASK_ID_COUNT || stage == NULL) {
        return;
    }

    snprintf(_taskProgress[taskId], sizeof(_taskProgress[taskId]), "%s", stage);

    #if defined(ESP32)
    snprintf(_rtcTaskProgress[taskId], sizeof(_rtcTaskProgress[taskId]), "%s", stage);
    _rtcCrashSnapshotMagic = RTC_CRASH_SNAPSHOT_MAGIC;
    #endif
}

const char* systemGetTaskProgress(TaskId_t taskId) {
    if (taskId < 0 || taskId >= TASK_ID_COUNT) {
        return "unknown";
    }

    return _taskProgress[taskId];
}

void systemSetTaskHandle(TaskId_t taskId, TaskHandle_t handle) {
    if (taskId >= 0 && taskId < TASK_ID_COUNT) {
        _taskHandles[taskId] = handle;
    }
}

unsigned long systemGetTaskHeartbeatAge(TaskId_t taskId) {
    if (taskId < 0 || taskId >= TASK_ID_COUNT) return 0;
    unsigned long hb = _taskHeartbeat[taskId];
    if (hb == 0) return 0;  // Not started yet

    unsigned long now = millis();
    if (hb > now) {
        return 0;
    }

    return now - hb;
}

bool systemCheckTaskHealth(void) {
    bool allOk = true;
    
    for (int i = 0; i < TASK_ID_COUNT; i++) {
        unsigned long hb = _taskHeartbeat[i];
        if (hb == 0) continue;  // Task not started yet

        unsigned long now = millis();
        if (hb > now) {
            continue;
        }

        unsigned long age = now - hb;
        if (age > TASK_STUCK_THRESHOLD_MS) {
            if (!_taskStuckLatched[i]) {
                LOG_ERROR("STUCK TASK: %s — no heartbeat for %lu seconds! (stage=%s)", 
                          TASK_NAMES[i], age / 1000, _taskProgress[i]);
                
                // Save to NVS once so we know after reboot without flooding writes.
                Preferences crashPrefs;
                crashPrefs.begin("crash", false);
                crashPrefs.putString("task", TASK_NAMES[i]);
                crashPrefs.putULong("age", age / 1000);
                crashPrefs.putULong("uptime", (millis() - _bootTime) / 1000);
                crashPrefs.putString("stage", _taskProgress[i]);
                crashPrefs.end();

                _taskStuckLatched[i] = true;
            }
            
            allOk = false;
        } else {
            _taskStuckLatched[i] = false;
        }
    }
    
    return allOk;
}

void systemPrintStackInfo(void) {
    for (int i = 0; i < TASK_ID_COUNT; i++) {
        if (_taskHandles[i] != NULL) {
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(_taskHandles[i]);
            unsigned long age = systemGetTaskHeartbeatAge((TaskId_t)i);
            LOG_INFO("Task %-12s | Stack free: %4u bytes | Heartbeat: %lums ago | Stage: %s",
                     TASK_NAMES[i], (unsigned int)(hwm * 4), age, _taskProgress[i]);
        }
    }
}

bool systemGetLastCrashInfo(char* buf, size_t bufSize) {
    if (_hasLastCrashInfoCache) {
        snprintf(buf, bufSize, "%s", _lastCrashInfoCache);
        return true;
    }

    Preferences crashPrefs;
    crashPrefs.begin("crash", true);  // Read-only
    
    String taskName = crashPrefs.getString("task", "");
    if (taskName.length() == 0) {
        crashPrefs.end();
        return false;  // No crash info
    }
    
    unsigned long age = crashPrefs.getULong("age", 0);
    unsigned long uptime = crashPrefs.getULong("uptime", 0);
    String stage = crashPrefs.getString("stage", "unknown");
    crashPrefs.end();
    
    snprintf(buf, bufSize, "Task '%s' stuck %lus (uptime was %lus, stage=%s)",
             taskName.c_str(), age, uptime, stage.c_str());
    snprintf(_lastCrashInfoCache, sizeof(_lastCrashInfoCache), "%s", buf);
    _hasLastCrashInfoCache = true;
    return true;
}

void systemReportLastCrash(void) {
    char crashInfo[128];
    if (systemGetLastCrashInfo(crashInfo, sizeof(crashInfo))) {
        snprintf(_lastCrashInfoCache, sizeof(_lastCrashInfoCache), "%s", crashInfo);
        _hasLastCrashInfoCache = true;

        LOG_WARN("=== LAST CRASH INFO ===");
        LOG_WARN("%s", crashInfo);
        LOG_WARN("Reset reason: %s", systemGetResetReasonString());
        LOG_WARN("=======================");
        
        // Clear crash info after reporting
        Preferences crashPrefs;
        crashPrefs.begin("crash", false);
        crashPrefs.clear();
        crashPrefs.end();
    }
}

