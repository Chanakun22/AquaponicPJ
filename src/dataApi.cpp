/**
 * @file dataApi.cpp
 * @brief Simple HTTP API for sensor monitoring
 * @note ไฟล์นี้สำหรับ Test เท่านั้น - ลบได้ถ้าไม่ใช้
 */

#include "dataApi.h"
#include "config.h"
#include "logger.h"
#include "system.h"
#include "wifiConn.h"
#include "netpie.h"
#include "phSensor.h"
#include "TdsSensor.h"
#include "dhtSensor.h"
#include "tempSensor.h"
#include "lightSensor.h"
#include "lightController.h"
#include <WebServer.h>
#include <WiFi.h>

// HTTP Server on port 80
static WebServer _httpServer(80);
static bool _serverStarted = false;

// ============================================================================
// CORS Helper
// ============================================================================

static void _setCorsHeaders() {
    _httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    _httpServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    _httpServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ============================================================================
// API Handlers
// ============================================================================

/**
 * @brief GET /api/sensors - ส่งค่าเซ็นเซอร์ทั้งหมด
 */
static void _handleSensors() {
    _setCorsHeaders();
    
    // Read all sensors
    float waterTemp = tempRead();
    float airTemp = dhtReadTemperature();
    float humidity = dhtReadHumidity();
    float tds = tdsRead(isnan(waterTemp) ? 25.0 : waterTemp);
    float ph = phRead();
    float light = lightRead();
    
    // Build JSON response
    String json = "{";
    json += "\"water_temp\":" + String(isnan(waterTemp) ? "null" : String(waterTemp, 2));
    json += ",\"air_temp\":" + String(isnan(airTemp) ? "null" : String(airTemp, 2));
    json += ",\"humidity\":" + String(isnan(humidity) ? "null" : String(humidity, 2));
    json += ",\"tds\":" + String(tds < 0 ? "null" : String(tds, 0));
    json += ",\"ph\":" + String(ph < 0 ? "null" : String(ph, 2));
    json += ",\"light\":" + String(light < 0 ? "null" : String(light, 0));
    json += ",\"light_state\":" + String(lightCtrlGetState() ? "true" : "false");
    json += "}";
    
    _httpServer.send(200, "application/json", json);
}

/**
 * @brief GET /api/health - ส่งข้อมูลสุขภาพระบบ
 */
static void _handleHealth() {
    _setCorsHeaders();
    
    SystemHealth_t health;
    systemGetHealth(&health);
    
    String json = "{";
    json += "\"uptime_sec\":" + String(health.uptimeMs / 1000);
    json += ",\"free_heap\":" + String(health.freeHeap);
    json += ",\"heap_size\":" + String(health.heapSize);
    json += ",\"min_free_heap\":" + String(health.minFreeHeap);
    json += ",\"cpu_temp\":" + String(health.cpuTemp, 1);
    json += ",\"wifi_reconnects\":" + String(health.wifiReconnects);
    json += ",\"mqtt_reconnects\":" + String(health.mqttReconnects);
    json += ",\"watchdog_resets\":" + String(health.watchdogResets);
    json += ",\"wifi_rssi\":" + String(WiFi.RSSI());
    json += ",\"mqtt_connected\":" + String(netpieIsConnected() ? "true" : "false");
    json += ",\"reset_reason\":\"" + String(health.resetReason) + "\"";
    json += "}";
    
    _httpServer.send(200, "application/json", json);
}

/**
 * @brief GET /api/info - ส่งข้อมูลระบบ
 */
static void _handleInfo() {
    _setCorsHeaders();
    
    String json = "{";
    json += "\"firmware\":\"" + String(systemGetVersion()) + "\"";
    json += ",\"hostname\":\"" + String(OTA_HOSTNAME) + "\"";
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
    json += "}";
    
    _httpServer.send(200, "application/json", json);
}

/**
 * @brief OPTIONS handler for CORS preflight
 */
static void _handleOptions() {
    _setCorsHeaders();
    _httpServer.send(204);
}

/**
 * @brief 404 handler
 */
static void _handleNotFound() {
    _setCorsHeaders();
    _httpServer.send(404, "application/json", "{\"error\":\"Not found\"}");
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void dataApiSetup(void) {
    // จะเริ่ม server เมื่อ WiFi เชื่อมต่อแล้ว
    LOG_INFO("[API] Data API ready (waiting for WiFi)");
}

void dataApiLoop(void) {
    // Start server when WiFi is connected
    if (!_serverStarted && wifiIsConnected()) {
        _httpServer.on("/api/sensors", HTTP_GET, _handleSensors);
        _httpServer.on("/api/health", HTTP_GET, _handleHealth);
        _httpServer.on("/api/info", HTTP_GET, _handleInfo);
        _httpServer.on("/api/sensors", HTTP_OPTIONS, _handleOptions);
        _httpServer.on("/api/health", HTTP_OPTIONS, _handleOptions);
        _httpServer.on("/api/info", HTTP_OPTIONS, _handleOptions);
        _httpServer.onNotFound(_handleNotFound);
        
        _httpServer.begin();
        _serverStarted = true;
        LOG_INFO("[API] HTTP Server started on port 80");
        LOG_INFO("[API] Endpoints: /api/sensors, /api/health, /api/info");
    }
    
    // Handle requests
    if (_serverStarted && wifiIsConnected()) {
        _httpServer.handleClient();
    }
    
    // Reset if WiFi lost
    if (_serverStarted && !wifiIsConnected()) {
        _serverStarted = false;
    }
}
