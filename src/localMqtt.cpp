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
#include "TdsSensor.h"  // For TDS calibration
#include "phSensor.h"   // For pH calibration


// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static WiFiClient _localWifiClient;
static PubSubClient _localMqtt(_localWifiClient);
static QueueHandle_t _logQueue = NULL;
static unsigned long _lastReconnectAttempt = 0;
static unsigned long _lastPublishTime = 0;
static IPAddress _brokerIp;
static bool _isIpResolved = false;
static uint8_t _connectionFailCount = 0;
static const uint8_t MAX_FAIL_BEFORE_RERESOLUTION = 3; // Re-resolve mDNS หลังล้มเหลว 3 ครั้ง
static unsigned long _reconnectInterval = 5000;          // Backoff interval (เริ่ม 5s, เพิ่มถึง 60s)
static const unsigned long MAX_RECONNECT_INTERVAL = 60000; // สูงสุด 60 วินาที
static void _onMqttMessage(char* topic, byte* payload, unsigned int length);
static bool _reconnect(void);
static void _publishSensorConfig(void);
static void _publishPhCalibrationStatus(void);

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief Attempt to resolve Pi Hostname via mDNS
 * @note Uses short timeout (1 sec) to avoid blocking sensors
 */
static bool _resolveBrokerIp() {
    LOG_INFO("Resolving mDNS: %s.local ...", LOCAL_MQTT_HOSTNAME);
    
    // Check if MDNS is running, if not start it
    if (!MDNS.begin(OTA_HOSTNAME)) {
        LOG_WARN("Error setting up MDNS responder!");
        // Continue anyway, maybe it was started in wifiConn?
    }

    // Query mDNS with SHORT timeout (200ms) to minimize blocking of Networking task
    // ลดจาก 1000ms เหลือ 200ms เพื่อลด blocking time เมื่อ Pi offline
    IPAddress ip = MDNS.queryHost(LOCAL_MQTT_HOSTNAME, 200);
    
    if (ip != IPAddress(0, 0, 0, 0)) {
        _brokerIp = ip;
        _isIpResolved = true;
        LOG_INFO("✅ Found Pi at IP: %s", _brokerIp.toString().c_str());
        return true;
    } else {
        // Fallback: ใช้ Static IP ของ Pi AP network
        if (_brokerIp.fromString(LOCAL_MQTT_STATIC_IP)) {
            _isIpResolved = true;
            LOG_INFO("⚡ mDNS failed, using Pi AP fallback IP: %s", LOCAL_MQTT_STATIC_IP);
            return true;
        }
        LOG_WARN("❌ Pi not found via mDNS or fallback. Retrying...");
        return false;
    }
}

/**
 * @brief MQTT Callback for receiving commands from Pi
 */
static void _onMqttMessage(char* topic, byte* payload, unsigned int length) {
    // Parse JSON payload
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    
    if (err) {
        LOG_ERROR("Local MQTT JSON parse error: %s", err.c_str());
        return;
    }
    
    // Handle TDS Calibration
    if (strcmp(topic, "aquaponics/config/tds_cal") == 0) {
        float lowPpm = doc["low_ppm"] | 0.0f;
        float lowVoltage = doc["low_voltage"] | 0.0f;
        float highPpm = doc["high_ppm"] | 0.0f;
        float highVoltage = doc["high_voltage"] | 0.0f;
        
        if (lowVoltage > 0 && highVoltage > 0 && lowVoltage != highVoltage) {
            tdsSetCalibration(lowPpm, lowVoltage, highPpm, highVoltage);
            LOG_INFO("TDS Calibration received from Pi!");
        } else {
            LOG_ERROR("Invalid TDS calibration data received");
        }
    }
    
    // Handle pH Calibration
    if (strcmp(topic, "aquaponics/config/ph_cal") == 0) {
        const char* action = doc["action"] | "";
        
        if (strcmp(action, "cal7") == 0) {
            if (phIsReady()) {
                phCalibratePh7();
                LOG_INFO("pH 7.0 Calibration triggered from Pi Dashboard!");
                _publishPhCalibrationStatus();
            } else {
                LOG_ERROR("pH sensor not ready for calibration");
            }
        } else if (strcmp(action, "cal4") == 0) {
            if (phIsReady()) {
                phCalibratePh4();
                LOG_INFO("pH 4.0 Calibration triggered from Pi Dashboard!");
                _publishPhCalibrationStatus();
            } else {
                LOG_ERROR("pH sensor not ready for calibration");
            }
        } else if (strcmp(action, "clear") == 0) {
            phClearCalibration();
            LOG_INFO("pH Calibration cleared from Pi Dashboard!");
            _publishPhCalibrationStatus();
        }
    }
    
    // Handle Sensor Toggle Configuration (batch update to prevent NVS race condition)
    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_SENSORS) == 0) {
        bool states[SENSOR_COUNT];
        // Read current states as defaults
        for (int i = 0; i < SENSOR_COUNT; i++) {
            states[i] = systemGetSensorEnabled((SensorId_t)i);
        }
        
        // Override with values from MQTT message
        if (doc.containsKey("tds")) states[SENSOR_TDS] = doc["tds"];
        if (doc.containsKey("ph")) states[SENSOR_PH] = doc["ph"];
        if (doc.containsKey("water")) states[SENSOR_WATER_TEMP] = doc["water"];
        if (doc.containsKey("air")) states[SENSOR_AIR_TEMP] = doc["air"];
        if (doc.containsKey("light")) states[SENSOR_LIGHT] = doc["light"];
        
        // Single NVS transaction for all sensors
        systemSetAllSensorsEnabled(states);
        
        LOG_INFO("Sensor config updated via MQTT (batch)");
        
        // Send feedback
        _publishSensorConfig();
    }
}

/**
 * @brief Attempt to connect to Local MQTT Broker
 */
static bool _reconnect() {
    // Re-resolve mDNS if connection failed multiple times (IP may have changed)
    if (_connectionFailCount >= MAX_FAIL_BEFORE_RERESOLUTION) {
        LOG_WARN("Multiple connection failures, re-resolving mDNS...");
        _isIpResolved = false;
        _connectionFailCount = 0;
    }
    
    if (!_isIpResolved) {
        if (!_resolveBrokerIp()) {
            _connectionFailCount++;
            return false;
        }
    }

    _localMqtt.setServer(_brokerIp, LOCAL_MQTT_PORT);
    _localMqtt.setCallback(_onMqttMessage);  // Set callback for incoming messages
    LOG_INFO("Connecting to Local MQTT (%s)...", _brokerIp.toString().c_str());

    // Create a random client ID
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "ESP32-Aquaponics-%04x", (unsigned int)random(0xffff));

    if (_localMqtt.connect(clientId)) {
        LOG_INFO("✅ Connected to Local MQTT!");
        _connectionFailCount = 0; // Reset counter on success
        
        // Subscribe to calibration and sensor config topics with QoS 1
        _localMqtt.subscribe("aquaponics/config/tds_cal", 1);
        _localMqtt.subscribe("aquaponics/config/ph_cal", 1);
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_CONFIG_SENSORS, 1);
        LOG_INFO("Subscribed to MQTT topics (QoS 1)");
        
        return true;
    } else {
        LOG_WARN("Local MQTT connect failed, rc=%d", _localMqtt.state());
        _connectionFailCount++;
        return false;
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void localMqttSetup(void) {
    LOG_INFO("Initializing Local MQTT...");
    
    // Set TCP socket timeout to 5 seconds (default ~30s causes task stuck detection)
    _localWifiClient.setTimeout(5);  // seconds
    
    _localMqtt.setBufferSize(512);
    _localMqtt.setSocketTimeout(5);  // PubSubClient keepalive timeout (seconds)
    
    // Create a queue for passing logs across tasks safely (20 items of 128 bytes)
    _logQueue = xQueueCreate(20, 128);
}

void localMqttLoop(void) {
    if (!wifiIsConnected()) return;

    if (!_localMqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            if (_reconnect()) {
                _lastReconnectAttempt = 0;
                _reconnectInterval = 5000; // รีเซ็ต backoff เมื่อเชื่อมต่อสำเร็จ
            } else {
                // Exponential backoff: 5s -> 10s -> 20s -> 40s -> 60s (max)
                _reconnectInterval = min(_reconnectInterval * 2, MAX_RECONNECT_INTERVAL);
                LOG_DEBUG("Next reconnect in %lu ms", _reconnectInterval);
            }
        }
    } else {
        _localMqtt.loop();
        
        // Process cross-core log queue safely in the Networking Task
        if (_logQueue != NULL) {
            char logBuff[128];
            int count = 0;
            // Process max 5 logs per loop to prevent starving other network tasks
            while (count < 5 && xQueueReceive(_logQueue, logBuff, 0) == pdTRUE) {
                _localMqtt.publish(LOCAL_MQTT_TOPIC_LOGS, logBuff);
                count++;
            }
        }
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
    
    // Add TDS voltage for calibration
    float tdsVoltage = tdsGetVoltage();
    if (tdsVoltage >= 0) doc["tds_voltage"] = round(tdsVoltage * 1000) / 1000.0;
    
    // Add pH voltage for calibration (mV)
    float phVoltage = phReadVoltage();
    if (phVoltage >= 0) doc["ph_voltage"] = round(phVoltage * 10) / 10.0;
    if (phIsReady()) doc["ph_value"] = round(phRead() * 100) / 100.0;
    
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
    doc["reset_reason"] = health.resetReason;
    doc["cpu_temp"] = health.cpuTemp; // ESP32 Temp

    char payload[512];
    serializeJson(doc, payload);

    if (_localMqtt.publish(LOCAL_MQTT_TOPIC_SENSORS, payload)) {
        LOG_DEBUG("Local MQTT Publish: %s", payload);
    } else {
        LOG_ERROR("Local MQTT Publish Failed");
    }
}

void localMqttPublishLog(const char* logMsg) {
    // If not connected to wifi, drop the log early
    if (!wifiIsConnected()) return;
    
    // Safety check: if queue is ready, push to queue (0 ticks = non-blocking)
    if (_logQueue != NULL) {
        xQueueSend(_logQueue, logMsg, 0);
    }
}

static void _publishSensorConfig(void) {
    if (!_localMqtt.connected()) return;

    StaticJsonDocument<256> doc;
    doc["tds"] = systemGetSensorEnabled(SENSOR_TDS);
    doc["ph"] = systemGetSensorEnabled(SENSOR_PH);
    doc["water"] = systemGetSensorEnabled(SENSOR_WATER_TEMP);
    doc["air"] = systemGetSensorEnabled(SENSOR_AIR_TEMP);
    doc["light"] = systemGetSensorEnabled(SENSOR_LIGHT);
    
    char buffer[256];
    serializeJson(doc, buffer);
    _localMqtt.publish(LOCAL_MQTT_TOPIC_STATUS_SENSORS, buffer);
    LOG_INFO("Sent Sensor Config Feedback: %s", buffer);
}

static void _publishPhCalibrationStatus(void) {
    if (!_localMqtt.connected()) return;

    StaticJsonDocument<256> doc;
    doc["ph_voltage"] = round(phReadVoltage() * 10) / 10.0;
    doc["ph_value"] = round(phRead() * 100) / 100.0;
    doc["calibrated"] = true;
    
    char buffer[256];
    serializeJson(doc, buffer);
    _localMqtt.publish("aquaponics/status/ph_cal", buffer);
    LOG_INFO("Sent pH Calibration Status: %s", buffer);
}


