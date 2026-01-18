/**
 * @file wifiConn.cpp
 * @brief WiFi Connection Manager using WiFiManager library (Non-blocking)
 * @details เซ็นเซอร์ทำงานได้ตลอดแม้ WiFi ไม่เชื่อมต่อ
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
static bool _portalRunning = false;
static bool _wifiSetupDone = false;

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void wifiSetup(void) {
    LOG_INFO("Initializing WiFiManager (Non-blocking)...");
    
    // เช็คปุ่ม BOOT เพื่อ reset WiFi settings หรือ factory reset
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    
    LOG_INFO("Hold BOOT button for %d seconds to factory reset...", FACTORY_RESET_TIME / 1000);
    
    // Non-blocking: ตรวจสอบปุ่ม
    unsigned long buttonCheckStart = millis();
    bool buttonPressed = false;
    
    while (millis() - buttonCheckStart < FACTORY_RESET_TIME) {
        if (digitalRead(FACTORY_RESET_PIN) == LOW) {
            buttonPressed = true;
            break;
        }
        yield();
    }
    
    if (buttonPressed) {
        LOG_WARN("Factory reset button pressed! Resetting all settings...");
        systemFactoryReset();
    }
    
    // ตั้งค่า WiFiManager แบบ Non-blocking
    _wifiMgr.setConfigPortalBlocking(false);  // ⚠️ สำคัญ! Non-blocking mode
    _wifiMgr.setConfigPortalTimeout(WIFI_CONNECT_TIMEOUT);
    _wifiMgr.setConnectTimeout(30);
    
    LOG_INFO("Connecting to saved WiFi or starting AP: %s", WIFI_AP_NAME);
    
    // เริ่ม autoConnect แบบ non-blocking
    if (_wifiMgr.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD)) {
        // เชื่อมต่อ WiFi ที่บันทึกไว้สำเร็จทันที
        _wifiConnected = true;
        LOG_INFO("========================================");
        LOG_INFO("WiFi Connected!");
        LOG_INFO("SSID: %s", WiFi.SSID().c_str());
        LOG_INFO("IP Address: %s", WiFi.localIP().toString().c_str());
        LOG_INFO("========================================");
        
        // ปิด power saving mode เพื่อความเสถียร
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
    } else {
        // ไม่มี WiFi ที่บันทึกไว้ หรือเชื่อมต่อไม่ได้ → AP Portal กำลังทำงาน
        _portalRunning = true;
        LOG_INFO("========================================");
        LOG_INFO("AP Mode - Config Portal Running");
        LOG_INFO("Connect to: %s", WIFI_AP_NAME);
        LOG_INFO("Open: http://192.168.4.1");
        LOG_INFO("Sensors will continue working!");
        LOG_INFO("========================================");
    }
    
    _wifiSetupDone = true;
}

void wifiLoop(void) {
    // ⚠️ สำคัญ! ต้องเรียก process() เมื่อ portal ทำงาน
    if (_portalRunning) {
        _wifiMgr.process();
        
        // เช็คว่าเชื่อมต่อสำเร็จหรือยัง
        if (WiFi.status() == WL_CONNECTED) {
            _portalRunning = false;
            _wifiConnected = true;
            
            LOG_INFO("========================================");
            LOG_INFO("WiFi Connected via Portal!");
            LOG_INFO("SSID: %s", WiFi.SSID().c_str());
            LOG_INFO("IP Address: %s", WiFi.localIP().toString().c_str());
            LOG_INFO("========================================");
            
            WiFi.setSleep(false);
            esp_wifi_set_ps(WIFI_PS_NONE);
        }
    }
    
    // ตรวจสอบสถานะ WiFi เป็นระยะ
    if (millis() - _wifiLastCheckTime >= WIFI_CHECK_INTERVAL) {
        _wifiLastCheckTime = millis();
        
        bool currentStatus = (WiFi.status() == WL_CONNECTED);
        
        // ตรวจจับการหลุด
        if (_wifiConnected && !currentStatus) {
            _wifiConnected = false;
            LOG_WARN("WiFi disconnected! Sensors still working...");
        }
        // ตรวจจับการเชื่อมต่อใหม่
        else if (!_wifiConnected && currentStatus && !_portalRunning) {
            _wifiConnected = true;
            systemIncrementWifiReconnects();
            LOG_INFO("WiFi reconnected! IP: %s", WiFi.localIP().toString().c_str());
        }
    }
}

bool wifiIsConnected(void) {
    return (WiFi.status() == WL_CONNECTED);
}

void wifiReset(void) {
    LOG_WARN("Resetting WiFi settings...");
    _wifiMgr.resetSettings();
    delay(500);
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
