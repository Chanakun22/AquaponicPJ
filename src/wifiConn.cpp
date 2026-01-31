/**
 * @file wifiConn.cpp
 * @brief WiFi Connection Manager using WiFiManager library (Non-blocking)
 * @details ใช้ WiFiManager แบบ Non-Blocking (setConfigPortalBlocking(false))
 */

#include "wifiConn.h"
#include "logger.h"
#include "system.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static WiFiManager _wifiMgr;
static unsigned long _wifiLastCheckTime = 0;
static bool _wifiConnected = false;
static unsigned long _factoryResetStartTime = 0;
static bool _factoryResetButtonPressed = false;

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief Handle factory reset button (non-blocking)
 */
static void _checkFactoryResetButton(void) {
    if (digitalRead(FACTORY_RESET_PIN) == LOW) {
        if (!_factoryResetButtonPressed) {
            _factoryResetButtonPressed = true;
            _factoryResetStartTime = millis();
        } else if (millis() - _factoryResetStartTime >= FACTORY_RESET_TIME) {
            LOG_WARN("Factory reset triggered!");
            systemFactoryReset();
        }
    } else {
        _factoryResetButtonPressed = false;
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void wifiSetup(void) {
    LOG_INFO("Initializing WiFiManager (Non-blocking)...");
    
    // Setup factory reset button
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    
    // Configure WiFiManager
    // IMPORTANT: Set non-blocking mode
    _wifiMgr.setConfigPortalBlocking(false);
    _wifiMgr.setConfigPortalTimeout(WIFI_CONNECT_TIMEOUT);
    _wifiMgr.setConnectTimeout(30);
    _wifiMgr.setDebugOutput(false);
    
    LOG_INFO("Connecting to saved WiFi or starting AP: %s", WIFI_AP_NAME);
    
    // Attempt connection
    bool connected = _wifiMgr.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD);
    
    if (connected) {
        _wifiConnected = true;
        LOG_INFO("========================================");
        LOG_INFO("WiFi Connected!");
        LOG_INFO("SSID: %s", WiFi.SSID().c_str());
        LOG_INFO("IP Address: %s", WiFi.localIP().toString().c_str());
        LOG_INFO("========================================");
        
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
    } else {
        LOG_INFO("========================================");
        LOG_INFO("WiFi Not Connected / Portal Running");
        LOG_INFO("Connect to AP: %s", WIFI_AP_NAME);
        LOG_INFO("Open: http://192.168.4.1");
        LOG_INFO("Sensors will continue working!");
        LOG_INFO("========================================");
    }
}

void wifiLoop(void) {
    // Process WiFiManager (handles config portal if active)
    _wifiMgr.process();
    
    // Check factory reset button (non-blocking)
    _checkFactoryResetButton();
    
    // Periodic connection check (non-blocking)
    if (millis() - _wifiLastCheckTime >= WIFI_CHECK_INTERVAL) {
        _wifiLastCheckTime = millis();
        
        bool currentStatus = (WiFi.status() == WL_CONNECTED);
        
        // Detect disconnect
        if (_wifiConnected && !currentStatus) {
            _wifiConnected = false;
            LOG_WARN("WiFi disconnected! Sensors still working...");
        }
        // Detect reconnect
        else if (!_wifiConnected && currentStatus) {
            _wifiConnected = true;
            systemIncrementWifiReconnects();
            LOG_INFO("WiFi Connected! IP: %s", WiFi.localIP().toString().c_str());
            
            // Should verify if portal needs to be stopped? 
            // WiFiManager handles this usually, but good to be sure.
        }
    }
}

bool wifiIsConnected(void) {
    return (WiFi.status() == WL_CONNECTED);
}

void wifiReset(void) {
    LOG_WARN("Resetting WiFi settings...");
    _wifiMgr.resetSettings();
    // ไม่ต้องใช้ delay - restart จะทำให้ทุกอย่างหยุดอยู่แล้ว
    ESP.restart();
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
