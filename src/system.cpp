/**
 * @file system.cpp
 * @brief System Management Implementation
 */

#include "system.h"
#include "logger.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>

#if defined(ESP32)
#include <ESP.h>
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
            // Note: Removed digitalWrite blinking to avoid conflict with RMT/NeoPixel on ESP32-S3
            LOG_INFO("Resetting in 3... 2... 1...");
            delay(1000);
            
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
    
    // Check if sensors are OK (simplified - can be enhanced)
    health->sensorsOk = true;  // TODO: Implement sensor health check
}

void systemFactoryReset(void) {
    LOG_WARN("Factory reset initiated!");
    
    // Clear all preferences
    Preferences prefs;
    
    // Clear WiFi settings
    prefs.begin("WiFiManager", false);
    prefs.clear();
    prefs.end();

    // Force erase WiFi credentials from NVS
    #if defined(ESP32)
    WiFi.disconnect(true, true);  // Turn off and erase credentials
    #endif
    
    // Clear pH calibration
    prefs.begin("phSensor", false);
    prefs.clear();
    prefs.end();
    
    // Clear system stats
    prefs.begin("system", false);
    prefs.clear();
    prefs.end();
    
    LOG_INFO("All settings cleared. Restarting...");
    
    delay(1000);
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


