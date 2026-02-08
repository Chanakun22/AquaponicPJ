/**
 * @file netpie.cpp
 * @brief Implementation สำหรับ NETPIE MQTT
 */

#include "netpie.h"
#include "logger.h"
#include "system.h"
#include "wifiConn.h"
#include "lightController.h"
#include "phSensor.h"
#include "localMqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static WiFiClient _wifiClient;
static PubSubClient _mqtt(_wifiClient);
static unsigned long _lastReconnectAttempt = 0;
static unsigned long _lastPublishTime = 0;
static bool _shadowRequested = false;

// ============================================================================
// PRIVATE FUNCTION PROTOTYPES
// ============================================================================

static void _mqttCallback(char* topic, byte* payload, unsigned int length);
static bool _mqttReconnect(void);
static void _parseShadowData(const char* json);

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief Parse Shadow data จาก NETPIE (รองรับ partial update)
 */
static void _parseShadowData(const char* json) {
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, json);
    
    if (err) {
        LOG_ERROR("JSON parse error: %s", err.c_str());
        return;
    }
    
    // ตรวจสอบ light schedule
    if (doc.containsKey("data")) {
        JsonObject data = doc["data"];
        
        // Light Schedule - อัพเดทเฉพาะ field ที่มี
        bool hasLightData = false;
        
        if (data.containsKey("lightEnabled")) {
            lightCtrlSetEnabled(data["lightEnabled"].as<int>());
            hasLightData = true;
        }
        if (data.containsKey("lightOnDay")) {
            lightCtrlSetOnDay(data["lightOnDay"].as<int>());
            hasLightData = true;
        }
        if (data.containsKey("lightOnTime")) {
            lightCtrlSetOnTime(data["lightOnTime"].as<const char*>());
            hasLightData = true;
        }
        if (data.containsKey("lightOffDay")) {
            lightCtrlSetOffDay(data["lightOffDay"].as<int>());
            hasLightData = true;
        }
        if (data.containsKey("lightOffTime")) {
            lightCtrlSetOffTime(data["lightOffTime"].as<const char*>());
            hasLightData = true;
        }
        
        // lightRelay: สั่งเปิด/ปิดไฟโดยตรง (0=OFF, 1=ON)
        // ทำงานได้เฉพาะเมื่อ lightEnabled = 0 (manual mode)
        if (data.containsKey("lightRelay")) {
            int relay = data["lightRelay"].as<int>();
            if (!lightCtrlIsEnabled()) {
                lightCtrlSetState(relay == 1);
                LOG_INFO("lightRelay: %s", relay == 1 ? "ON" : "OFF");
            } else {
                LOG_DEBUG("lightRelay IGNORED (schedule mode active)");
            }
        }
        
        if (hasLightData) {
            lightCtrlPrintSchedule();
            

            

        }
        
        // pH Calibration Commands
        if (data.containsKey("phCal7")) {
            if (data["phCal7"].as<int>() == 1) {
                LOG_INFO("pH Calibration 7.0 triggered via NETPIE");
                phCalibratePh7();
            }
        }
        if (data.containsKey("phCal4")) {
            if (data["phCal4"].as<int>() == 1) {
                LOG_INFO("pH Calibration 4.0 triggered via NETPIE");
                phCalibratePh4();
            }
        }
    }
}

/**
 * @brief Callback เมื่อได้รับข้อความจาก NETPIE
 */
static void _mqttCallback(char* topic, byte* payload, unsigned int length) {
    LOG_DEBUG("MQTT message received: %s", topic);
    
    // แปลง payload เป็น string
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    
    // Shadow response (ตอนเริ่มต้น) หรือ Shadow updated (real-time)
    if (strstr(topic, "@shadow/data/get/response") || 
        strstr(topic, "@private/shadow/data/get/response") ||
        strstr(topic, "@shadow/data/updated")) {
        LOG_DEBUG("Shadow data received/updated");
        _parseShadowData(message);
    }
    
    // Message topics
    if (strstr(topic, "@msg/lightEnabled")) {
        int enabled = atoi(message);
        StaticJsonDocument<256> doc;
        doc["data"]["lightEnabled"] = enabled;
        char json[256];
        serializeJson(doc, json);
        _parseShadowData(json);
    }
    
    if (strstr(topic, "@msg/lightOn")) {
        lightCtrlSetState(true);
        _mqtt.publish("@shadow/data/update", "{\"data\":{\"lightRelay\":1}}");
    }
    
    if (strstr(topic, "@msg/lightOff")) {
        lightCtrlSetState(false);
        _mqtt.publish("@shadow/data/update", "{\"data\":{\"lightRelay\":0}}");
    }
}

/**
 * @brief พยายามเชื่อมต่อ MQTT
 */
static bool _mqttReconnect(void) {
    LOG_INFO("Connecting to MQTT broker...");
    
    // DEBUG: Check credentials (remove specific values after debugging)
    LOG_DEBUG("ID: %s", NETPIE_CLIENT_ID);
    LOG_DEBUG("Token: %s", NETPIE_TOKEN);
    // LOG_DEBUG("Secret: %s", NETPIE_SECRET); // Keep secret hidden but check ID/Token format
    
    // NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET are macros from config.h/secrets.ini
    if (_mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET)) {
        LOG_INFO("MQTT connected!");
        
        // Subscribe topics
        _mqtt.subscribe("@msg/#");
        _mqtt.subscribe("@private/shadow/data/get/response");
        _mqtt.subscribe("@shadow/data/get/response");
        _mqtt.subscribe("@shadow/data/updated");
        
        LOG_DEBUG("Subscribed to MQTT topics");
        
        // Request shadow data
        _mqtt.publish("@shadow/data/get", "{}");
        _shadowRequested = true;
        
        return true;
    } else {
        LOG_WARN("MQTT connection failed, rc=%d", _mqtt.state());
        return false;
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void netpieSetup(void) {
    LOG_INFO("Initializing NETPIE MQTT...");
    
    _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    _mqtt.setCallback(_mqttCallback);
    _mqtt.setBufferSize(1024);  // เพิ่ม buffer size
    
    LOG_INFO("MQTT Broker: %s:%d", MQTT_BROKER, MQTT_PORT);
}

void netpieLoop(void) {
    if (!wifiIsConnected()) {
        return;
    }
    
    if (!_mqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= MQTT_RECONNECT_INTERVAL) {
            _lastReconnectAttempt = now;
            if (_mqttReconnect()) {
                _lastReconnectAttempt = 0;
                systemIncrementMqttReconnects();
            }
        }
    } else {
        _mqtt.loop();
    }
}

bool netpieIsConnected(void) {
    return _mqtt.connected();
}

void netpiePublishData(float waterTemp, float airTemp, float humidity, float tds, float light, float ph) {
    if (millis() - _lastPublishTime < NETPIE_PUBLISH_INTERVAL) {
        return;
    }
    _lastPublishTime = millis();
    
    if (!netpieIsConnected()) {
        return;
    }
    
    StaticJsonDocument<512> doc;
    JsonObject data = doc.createNestedObject("data");
    
    if (!isnan(waterTemp)) {
        data["water_temp"] = round(waterTemp * 10) / 10.0;
    }
    if (!isnan(airTemp)) {
        data["air_temp"] = round(airTemp * 10) / 10.0;
    }
    if (!isnan(humidity)) {
        data["humidity"] = round(humidity * 10) / 10.0;
    }
    if (tds >= 0) {
        data["tds"] = round(tds * 10) / 10.0;
    }
    if (light >= 0) {
        data["light"] = round(light * 10) / 10.0;
    }
    if (ph >= 0) {
        data["ph"] = round(ph * 100) / 100.0;  // 2 decimal places for pH
    }
    
    // เพิ่มสถานะไฟ
    data["light_relay"] = lightCtrlGetState() ? 1 : 0;
    
    char payload[512];
    serializeJson(doc, payload);
    
    if (_mqtt.publish("@shadow/data/update", payload)) {
        LOG_DEBUG("Published to NETPIE: %s", payload);
    } else {
        LOG_ERROR("NETPIE publish failed!");
    }
}

void netpiePublish(const char* topic, const char* payload) {
    if (!netpieIsConnected()) {
        return;
    }
    _mqtt.publish(topic, payload);
}
