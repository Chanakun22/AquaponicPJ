/**
 * @file webServer.cpp
 * @brief Web Server & Web Serial Implementation
 */

#include "webServer.h"
#include "logger.h"
#include "config.h"
#include "secrets.h"
#include "system.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static AsyncWebServer _server(80);

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

static void _recvMsg(uint8_t *data, size_t len) {
    String msg = "";
    for(size_t i=0; i < len; i++){
        msg += char(data[i]);
    }
    
    LOG_INFO("WebSerial Received: %s", msg.c_str());
    
    // Simple command handling
    if (msg == "reset") {
        LOG_WARN("WebSerial: Reset command received");
        delay(1000);
        ESP.restart();
    } else if (msg == "health") {
        SystemHealth_t health;
        systemGetHealth(&health);
        WebSerial.println("===== SYSTEM HEALTH =====");
        WebSerial.printf("Uptime: %lu s\n", health.uptimeMs / 1000);
        WebSerial.printf("Free Heap: %lu bytes\n", health.freeHeap);
        WebSerial.println("=========================");
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void webServerSetup(void) {
    LOG_INFO("Initializing Web Server & WebSerial...");

    // WebSerial Setup
    WebSerial.begin(&_server);
    WebSerial.setID("admin"); // Optional: set user ID
    
    // Set Password if defined
    #ifdef SECRET_OTA_PASSWORD
    if (strlen(SECRET_OTA_PASSWORD) > 0) {
        WebSerial.setAuthentication("admin", SECRET_OTA_PASSWORD);
    }
    #endif

    WebSerial.msgCallback(_recvMsg);

    // Root URL
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><head><title>Aquaponics Sensor</title></head><body>";
        html += "<h1>Aquaponics Sensor System</h1>";
        html += "<p>Status: <b>Online</b></p>";
        html += "<p>System Version: " + String(systemGetVersion()) + "</p>";
        html += "<p>Uptime: " + String(systemGetUptimeSeconds()) + " seconds</p>";
        html += "<p><a href='/webserial'>Go to Web Serial Console</a></p>";
        html += "</body></html>";
        request->send(200, "text/html", html);
    });

    _server.begin();
    LOG_INFO("Web Server started on port 80");
    LOG_INFO("Web Serial available at: /webserial");
}
