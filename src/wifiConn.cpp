/**
 * @file wifiConn.cpp
 * @brief WiFi Connection — Fixed to Pi AP (Aquaponics-LAN)
 * @details เชื่อมต่อ WiFi ตรงไปยัง Pi Hotspot โดยไม่ใช้ WiFiManager
 */

#include "wifiConn.h"
#include "logger.h"
#include "system.h"
#include <WiFi.h>
#include <esp_wifi.h>

#if defined(ESP32) && WATCHDOG_ENABLED
#include "esp_task_wdt.h"
#endif

// ============================================================================
// FIXED WIFI CREDENTIALS (Pi Hotspot)
// ============================================================================

static const char* WIFI_SSID = WIFI_AP_NAME;
static const char* WIFI_PASS = WIFI_AP_PASS;

static bool wifiCredentialsConfigured(void) {
    return strlen(WIFI_SSID) > 0
        && strlen(WIFI_PASS) > 0
        && strcmp(WIFI_PASS, UNCONFIGURED_SECRET_SENTINEL) != 0;
}

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static unsigned long _wifiLastCheckTime = 0;
static bool _wifiConnected = false;
static unsigned long _lastReconnectAttempt = 0;
static const unsigned long RECONNECT_INTERVAL = 10000; // reconnect ทุก 10 วินาที

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void wifiSetup(void) {
    if (!wifiCredentialsConfigured()) {
        LOG_WARN("WiFi disabled: SECRET_WIFI_AP_PASS is not configured");
        return;
    }

    LOG_INFO("Connecting to fixed WiFi: %s", WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    // รอเชื่อมต่อสูงสุด 10 วินาที (non-blocking style)
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
        #if defined(ESP32) && WATCHDOG_ENABLED
        esp_task_wdt_reset();
        #endif
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        _wifiConnected = true;
        esp_wifi_set_ps(WIFI_PS_NONE);
        
        LOG_INFO("========================================");
        LOG_INFO("WiFi Connected!");
        LOG_INFO("SSID: %s", WiFi.SSID().c_str());
        LOG_INFO("IP Address: %s", WiFi.localIP().toString().c_str());
        LOG_INFO("RSSI: %d dBm", WiFi.RSSI());
        LOG_INFO("========================================");
    } else {
        LOG_WARN("========================================");
        LOG_WARN("WiFi Not Connected — will retry in background");
        LOG_WARN("Target SSID: %s", WIFI_SSID);
        LOG_WARN("========================================");
    }
}

void wifiLoop(void) {
    if (!wifiCredentialsConfigured()) {
        return;
    }

    // Periodic connection check (non-blocking)
    if (millis() - _wifiLastCheckTime >= WIFI_CHECK_INTERVAL) {
        _wifiLastCheckTime = millis();
        
        bool currentStatus = (WiFi.status() == WL_CONNECTED);
        
        // Detect disconnect
        if (_wifiConnected && !currentStatus) {
            _wifiConnected = false;
            LOG_WARN("WiFi disconnected! Will attempt reconnection...");
            _lastReconnectAttempt = 0;
        }
        // Detect reconnect
        else if (!_wifiConnected && currentStatus) {
            _wifiConnected = true;
            systemIncrementWifiReconnects();
            LOG_INFO("WiFi Reconnected! IP: %s", WiFi.localIP().toString().c_str());
            
            WiFi.setSleep(false);
            esp_wifi_set_ps(WIFI_PS_NONE);
        }
    }
    
    // Auto-reconnect when disconnected (non-blocking)
    if (!_wifiConnected && WiFi.status() != WL_CONNECTED) {
        if (millis() - _lastReconnectAttempt >= RECONNECT_INTERVAL) {
            _lastReconnectAttempt = millis();
            LOG_INFO("Attempting WiFi reconnection to %s...", WIFI_SSID);
            
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
    }
}

bool wifiIsConnected(void) {
    return (WiFi.status() == WL_CONNECTED);
}

void wifiReset(void) {
    if (!wifiCredentialsConfigured()) {
        LOG_WARN("WiFi reset ignored: SECRET_WIFI_AP_PASS is not configured");
        return;
    }

    LOG_WARN("Resetting WiFi — reconnecting to %s", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool wifiGetIP(char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize < 16) {
        return false;
    }
    
    if (wifiIsConnected()) {
        IPAddress ip = WiFi.localIP();
        snprintf(buffer, bufferSize, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        return true;
    }
    
    strncpy(buffer, "Not connected", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return false;
}

void wifiMarkServerStarted(void) {
    // Not used
}
