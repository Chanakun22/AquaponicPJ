/**
 * @file wifiConn.cpp
 * @brief WiFi Connection Manager using WiFiManager library (Non-blocking)
 * @details เซ็นเซอร์ทำงานได้ตลอดแม้ WiFi ไม่เชื่อมต่อ
 */

#include "wifiConn.h"
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
    Serial.println(F("[WIFI] Initializing WiFiManager (Non-blocking)..."));
    
    // เช็คปุ่ม BOOT เพื่อ reset WiFi settings
    pinMode(0, INPUT_PULLUP);  // BOOT button on ESP32
    
    Serial.println(F("[WIFI] Hold BOOT button to reset WiFi settings..."));
    
    // Non-blocking: ตรวจสอบปุ่มเป็นเวลา 2 วินาที
    unsigned long buttonCheckStart = millis();
    while (millis() - buttonCheckStart < 2000) {
        if (digitalRead(0) == LOW) {
            Serial.println(F("[WIFI] BOOT button pressed! Resetting WiFi..."));
            _wifiMgr.resetSettings();
            Serial.println(F("[WIFI] WiFi settings erased. Restarting..."));
            // รอให้ serial messages ถูกส่ง (non-blocking)
            unsigned long flushStart = millis();
            while (millis() - flushStart < 100) {
                yield();  // ให้เวลาระบบทำงาน
            }
            ESP.restart();
        }
        yield();  // ให้เวลาระบบทำงานระหว่างตรวจสอบปุ่ม
    }
    
    // ตั้งค่า WiFiManager แบบ Non-blocking
    _wifiMgr.setConfigPortalBlocking(false);  // ⚠️ สำคัญ! Non-blocking mode
    _wifiMgr.setConfigPortalTimeout(WIFI_CONNECT_TIMEOUT);
    _wifiMgr.setConnectTimeout(30);
    
    Serial.print(F("[WIFI] Connecting to saved WiFi or starting AP: "));
    Serial.println(WIFI_AP_NAME);
    
    // เริ่ม autoConnect แบบ non-blocking
    if (_wifiMgr.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD)) {
        // เชื่อมต่อ WiFi ที่บันทึกไว้สำเร็จทันที
        _wifiConnected = true;
        Serial.println(F("[WIFI] ========================================"));
        Serial.println(F("[WIFI] Connected!"));
        Serial.print(F("[WIFI] SSID: "));
        Serial.println(WiFi.SSID());
        Serial.print(F("[WIFI] IP Address: "));
        Serial.println(WiFi.localIP());
        Serial.println(F("[WIFI] ========================================"));
        
        // ปิด power saving mode เพื่อความเสถียร
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
    } else {
        // ไม่มี WiFi ที่บันทึกไว้ หรือเชื่อมต่อไม่ได้ → AP Portal กำลังทำงาน
        _portalRunning = true;
        Serial.println(F("[WIFI] ========================================"));
        Serial.println(F("[WIFI] AP Mode - Config Portal Running"));
        Serial.print(F("[WIFI] Connect to: "));
        Serial.println(WIFI_AP_NAME);
        Serial.println(F("[WIFI] Open: http://192.168.4.1"));
        Serial.println(F("[WIFI] Sensors will continue working!"));
        Serial.println(F("[WIFI] ========================================"));
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
            
            Serial.println(F("[WIFI] ========================================"));
            Serial.println(F("[WIFI] Connected via Portal!"));
            Serial.print(F("[WIFI] SSID: "));
            Serial.println(WiFi.SSID());
            Serial.print(F("[WIFI] IP Address: "));
            Serial.println(WiFi.localIP());
            Serial.println(F("[WIFI] ========================================"));
            
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
            Serial.println(F("[WIFI] Disconnected! Sensors still working..."));
        }
        // ตรวจจับการเชื่อมต่อใหม่
        else if (!_wifiConnected && currentStatus && !_portalRunning) {
            _wifiConnected = true;
            Serial.println(F("[WIFI] Reconnected!"));
            Serial.print(F("[WIFI] IP: "));
            Serial.println(WiFi.localIP());
        }
    }
}

bool wifiIsConnected(void) {
    return (WiFi.status() == WL_CONNECTED);
}

void wifiReset(void) {
    Serial.println(F("[WIFI] Resetting settings..."));
    _wifiMgr.resetSettings();
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
