/**
 * @file localMqtt.cpp
 * @brief Implementation of Local MQTT for Raspberry Pi
 */

#include "localMqtt.h"
#include "config.h"
#include "logger.h"
#include "system.h"
#include "wifiConn.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "netpie.h"

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static WiFiClient _localWifiClient;
static PubSubClient _localMqtt(_localWifiClient);
static unsigned long _lastReconnectAttempt = 0;
static unsigned long _lastPublishTime = 0;
static IPAddress _brokerIp;
static bool _isIpResolved = false;

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief Attempt to resolve Pi Hostname via mDNS
 */
static bool _resolveBrokerIp() {
    LOG_INFO("Resolving mDNS: %s.local ...", LOCAL_MQTT_HOSTNAME);
    
    // Check if MDNS is running, if not start it
    if (!MDNS.begin(OTA_HOSTNAME)) {
        LOG_WARN("Error setting up MDNS responder!");
        // Continue anyway, maybe it was started in wifiConn?
    }

    // Query mDNS
    IPAddress ip = MDNS.queryHost(LOCAL_MQTT_HOSTNAME);
    
    if (ip != IPAddress(0, 0, 0, 0)) {
        _brokerIp = ip;
        _isIpResolved = true;
        LOG_INFO("✅ Found Pi at IP: %s", _brokerIp.toString().c_str());
        return true;
    } else {
        LOG_WARN("❌ Pi not found via mDNS. Retrying...");
        return false;
    }
}

/**
 * @brief Attempt to connect to Local MQTT Broker
 */
static bool _reconnect() {
    if (!_isIpResolved) {
        if (!_resolveBrokerIp()) {
            return false;
        }
    }

    _localMqtt.setServer(_brokerIp, LOCAL_MQTT_PORT);
    LOG_INFO("Connecting to Local MQTT (%s)...", _brokerIp.toString().c_str());

    // Create a random client ID
    String clientId = "ESP32-Aquaponics-" + String(random(0xffff), HEX);

    if (_localMqtt.connect(clientId.c_str())) {
        LOG_INFO("✅ Connected to Local MQTT!");
        // Optional: Subscribe to local control topics here
        return true;
    } else {
        LOG_WARN("Local MQTT connect failed, rc=%d", _localMqtt.state());
        return false;
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void localMqttSetup(void) {
    LOG_INFO("Initializing Local MQTT...");
    _localMqtt.setBufferSize(512);
}

void localMqttLoop(void) {
    if (!wifiIsConnected()) return;

    if (!_localMqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            if (_reconnect()) {
                _lastReconnectAttempt = 0;
            }
        }
    } else {
        _localMqtt.loop();
    }
}

bool localMqttIsConnected(void) {
    return _localMqtt.connected();
}

void localMqttPublishData(float waterTemp, float airTemp, float humidity, float tds, float light, float ph) {
    if (millis() - _lastPublishTime < LOCAL_PUBLISH_INTERVAL) {
        return;
    }
    
    if (!localMqttIsConnected()) return;
    
    _lastPublishTime = millis();

    StaticJsonDocument<512> doc;
    
    // Format same as Netpie for consistency, or simpler flat JSON
    if (!isnan(waterTemp)) doc["water_temp"] = round(waterTemp * 10) / 10.0;
    if (!isnan(airTemp)) doc["air_temp"] = round(airTemp * 10) / 10.0;
    if (!isnan(humidity)) doc["humidity"] = round(humidity * 10) / 10.0;
    if (tds >= 0) doc["tds"] = round(tds * 10) / 10.0;
    if (light >= 0) doc["light"] = round(light * 10) / 10.0;
    if (ph >= 0) doc["ph"] = round(ph * 100) / 100.0;
    
    // Add Network Connectivity Status
    doc["mqtt_connected"] = netpieIsConnected(); // Status for Dashboard
    doc["wifi_rssi"] = WiFi.RSSI();
    
    // Add System Health Stats
    SystemHealth_t health;
    systemGetHealth(&health);
    doc["uptime_sec"] = health.uptimeMs / 1000;
    doc["free_heap"] = health.freeHeap;
    doc["heap_size"] = health.heapSize; // Added for RAM calc
    doc["wifi_reconnects"] = health.wifiReconnects;
    doc["mqtt_reconnects"] = health.mqttReconnects;
    doc["watchdog_resets"] = health.watchdogResets;
    doc["reset_reason"] = String(health.resetReason);
    doc["cpu_temp"] = health.cpuTemp; // ESP32 Temp

    char payload[512];
    serializeJson(doc, payload);

    if (_localMqtt.publish(LOCAL_MQTT_TOPIC_SENSORS, payload)) {
        LOG_DEBUG("Local MQTT Publish: %s", payload);
    } else {
        LOG_ERROR("Local MQTT Publish Failed");
    }
}
