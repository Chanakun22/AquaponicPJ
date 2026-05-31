/**
 * @file netpie.cpp
 * @brief Implementation สำหรับ NETPIE MQTT
 */

#include "netpie.h"
#include "logger.h"
#include "system.h"
#include "wifiConn.h"
#include "lightController.h"
#include "fishFeeder.h"
#include "phSensor.h"
#include "localMqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#if defined(ESP32) && WATCHDOG_ENABLED
#include "esp_task_wdt.h"
#endif

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static WiFiClient _wifiClient;
static PubSubClient _mqtt(_wifiClient);
static unsigned long _lastReconnectAttempt = 0;
static unsigned long _lastPublishTime = 0;
static bool _shadowRequested = false;
static bool _lastFeedNowShadow = false;
static const size_t NETPIE_SHADOW_JSON_CAPACITY = 2048;
static const size_t NETPIE_MQTT_MESSAGE_BUFFER_SIZE = 2048;
static const unsigned long NETPIE_LOCAL_PRIORITY_RETRY_INTERVAL = 120000;
static const unsigned long NETPIE_CLOUD_UNREACHABLE_COOLDOWN_MS = 1800000;
static const uint8_t NETPIE_CLOUD_UNREACHABLE_FAIL_THRESHOLD = 3;

// Exponential backoff for reconnection
static unsigned long _reconnectInterval = MQTT_RECONNECT_INTERVAL;  // Start at 5s
static const unsigned long MAX_NETPIE_RECONNECT_INTERVAL = 60000;   // Max 60s
static unsigned long _cloudRetrySuspendedUntil = 0;
static uint8_t _cloudConnectFailStreak = 0;
static int _lastConnectErrorState = MQTT_CONNECTED;

// ============================================================================
// PRIVATE FUNCTION PROTOTYPES
// ============================================================================

static void _mqttCallback(char* topic, byte* payload, unsigned int length);
static bool _mqttReconnect(void);
static void _parseShadowData(const char* json);
static const char* _mqttStateName(int state);
static bool _cloudRetrySuspended(unsigned long now);

static void _networkTaskCheckpoint(const char* stage) {
    systemSetTaskProgress(TASK_NETWORKING, stage);
    systemTaskHeartbeat(TASK_NETWORKING);

    #if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_reset();
    #endif
}

static const char* _mqttStateName(int state) {
    switch (state) {
        case MQTT_CONNECTION_TIMEOUT:
            return "connection_timeout";
        case MQTT_CONNECTION_LOST:
            return "connection_lost";
        case MQTT_CONNECT_FAILED:
            return "tcp_connect_failed";
        case MQTT_DISCONNECTED:
            return "disconnected";
        case MQTT_CONNECTED:
            return "connected";
        case MQTT_CONNECT_BAD_PROTOCOL:
            return "bad_protocol";
        case MQTT_CONNECT_BAD_CLIENT_ID:
            return "bad_client_id";
        case MQTT_CONNECT_UNAVAILABLE:
            return "server_unavailable";
        case MQTT_CONNECT_BAD_CREDENTIALS:
            return "bad_credentials";
        case MQTT_CONNECT_UNAUTHORIZED:
            return "unauthorized";
        default:
            return "unknown";
    }
}

static bool _cloudRetrySuspended(unsigned long now) {
    return _cloudRetrySuspendedUntil != 0 && (long)(now - _cloudRetrySuspendedUntil) < 0;
}

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief Parse Shadow data จาก NETPIE (รองรับ partial update)
 */
static void _parseShadowData(const char* json) {
    StaticJsonDocument<NETPIE_SHADOW_JSON_CAPACITY> doc;
    DeserializationError err = deserializeJson(doc, json);
    
    if (err) {
        LOG_ERROR("JSON parse error: %s (payload_len=%u, doc_cap=%u)",
                  err.c_str(),
                  static_cast<unsigned int>(strlen(json)),
                  static_cast<unsigned int>(NETPIE_SHADOW_JSON_CAPACITY));
        return;
    }
    
    // ตรวจสอบ light schedule
    if (doc.containsKey("data")) {
        JsonObject data = doc["data"];

        if (lightCtrlAllowsNetpieControl()) {
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

            if (data.containsKey("lightRelay")) {
                int relay = data["lightRelay"].as<int>();
                lightCtrlSetManualState(relay == 1);
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
        } else if (data.containsKey("lightEnabled") || data.containsKey("lightOnDay") ||
                   data.containsKey("lightOnTime") || data.containsKey("lightOffDay") ||
                   data.containsKey("lightOffTime") || data.containsKey("lightRelay")) {
            LOG_WARN("NETPIE light command ignored because control source is not netpie");
        }

        bool feedNowShadow = data["feedNow"] | false;

        if (fishFeederAllowsNetpieControl()) {
            bool hasFeederData = false;

            if (data.containsKey("feederEnabled")) {
                fishFeederSetEnabled(data["feederEnabled"].as<int>() == 1);
                hasFeederData = true;
            }
            if (data.containsKey("feederDay")) {
                fishFeederSetFeedDay(data["feederDay"].as<int>());
                hasFeederData = true;
            }
            if (data.containsKey("feederTime")) {
                fishFeederSetFeedTime(data["feederTime"].as<const char*>());
                hasFeederData = true;
            }
            if (data.containsKey("feederDurationMs")) {
                fishFeederSetDurationMs(data["feederDurationMs"].as<unsigned long>());
                hasFeederData = true;
            }
            if (feedNowShadow && !_lastFeedNowShadow) {
                fishFeederStartManualFeed("Manual feed triggered from NETPIE");
                hasFeederData = true;
                _mqtt.publish("@shadow/data/update", "{\"data\":{\"feedNow\":false}}");
            }

            if (hasFeederData) {
                LOG_INFO("Fish feeder config updated from NETPIE");
            }
        } else if (data.containsKey("feederEnabled") || data.containsKey("feederDay") ||
                   data.containsKey("feederTime") || data.containsKey("feederDurationMs") ||
                   feedNowShadow) {
            LOG_WARN("NETPIE fish feeder command ignored because control source is not netpie");
        }

        _lastFeedNowShadow = data.containsKey("feedNow") ? feedNowShadow : false;
        
        // pH Calibration Commands
        if (data.containsKey("phCal686") || data.containsKey("phCal7")) {
            int phCal686 = data.containsKey("phCal686") ? data["phCal686"].as<int>() : data["phCal7"].as<int>();
            if (phCal686 == 1) {
                LOG_INFO("pH Calibration 6.86 triggered via NETPIE");
                phCalibratePh686();
            }
        }
        if (data.containsKey("phCal401") || data.containsKey("phCal4")) {
            int phCal401 = data.containsKey("phCal401") ? data["phCal401"].as<int>() : data["phCal4"].as<int>();
            if (phCal401 == 1) {
                LOG_INFO("pH Calibration 4.01 triggered via NETPIE");
                phCalibratePh401();
            }
        }
        if (data.containsKey("phCal918")) {
            if (data["phCal918"].as<int>() == 1) {
                LOG_INFO("pH Calibration 9.18 triggered via NETPIE");
                phCalibratePh918();
            }
        }
    }
}

/**
 * @brief Callback เมื่อได้รับข้อความจาก NETPIE
 */
static void _mqttCallback(char* topic, byte* payload, unsigned int length) {
    LOG_DEBUG("MQTT message received: %s", topic);
    
    // ใช้ static buffer แทน VLA เพื่อป้องกัน stack overflow
    static char message[NETPIE_MQTT_MESSAGE_BUFFER_SIZE];
    size_t copyLen = (length < sizeof(message) - 1) ? length : sizeof(message) - 1;
    memcpy(message, payload, copyLen);
    message[copyLen] = '\0';

    if (copyLen != length) {
        LOG_WARN("NETPIE payload truncated from %u to %u bytes", length, static_cast<unsigned int>(copyLen));
    }
    
    // Shadow response (ตอนเริ่มต้น) หรือ Shadow updated (real-time)
    if (strstr(topic, "@shadow/data/get/response") || 
        strstr(topic, "@private/shadow/data/get/response") ||
        strstr(topic, "@shadow/data/updated")) {
        LOG_DEBUG("Shadow data received/updated");
        _parseShadowData(message);
    }
    
    // Message topics
    if (strstr(topic, "@msg/lightEnabled")) {
        if (!lightCtrlAllowsNetpieControl()) {
            LOG_WARN("NETPIE @msg/lightEnabled ignored because control source is not netpie");
            return;
        }
        int enabled = atoi(message);
        StaticJsonDocument<256> doc;
        doc["data"]["lightEnabled"] = enabled;
        char json[256];
        serializeJson(doc, json);
        _parseShadowData(json);
    }
    
    if (strstr(topic, "@msg/lightOn")) {
        if (lightCtrlAllowsNetpieControl()) {
            lightCtrlSetManualState(true);
            lightCtrlSetState(true);
            _mqtt.publish("@shadow/data/update", "{\"data\":{\"lightRelay\":1}}");
        } else {
            LOG_WARN("NETPIE @msg/lightOn ignored because control source is not netpie");
        }
    }
    
    if (strstr(topic, "@msg/lightOff")) {
        if (lightCtrlAllowsNetpieControl()) {
            lightCtrlSetManualState(false);
            lightCtrlSetState(false);
            _mqtt.publish("@shadow/data/update", "{\"data\":{\"lightRelay\":0}}");
        } else {
            LOG_WARN("NETPIE @msg/lightOff ignored because control source is not netpie");
        }
    }

    if (strstr(topic, "@msg/feedNow")) {
        if (fishFeederAllowsNetpieControl()) {
            fishFeederStartManualFeed("Manual feed triggered from NETPIE @msg");
        } else {
            LOG_WARN("NETPIE @msg/feedNow ignored because control source is not netpie");
        }
    }
}

/**
 * @brief พยายามเชื่อมต่อ MQTT
 */
static bool _mqttReconnect(void) {
    LOG_INFO("Connecting to MQTT broker...");
    LOG_DEBUG(
        "MQTT credentials present: client_id=%s, token=%s",
        NETPIE_CLIENT_ID,
        strlen(NETPIE_TOKEN) > 0 ? "configured" : "missing"
    );
    
    // NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET are macros from config.h/secrets.ini
    _networkTaskCheckpoint("netpie_connect");
    if (_mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET)) {
        _networkTaskCheckpoint("netpie_connected");
        LOG_INFO("MQTT connected!");
        _cloudRetrySuspendedUntil = 0;
        _cloudConnectFailStreak = 0;
        _lastConnectErrorState = MQTT_CONNECTED;
        
        // Subscribe topics
        _mqtt.subscribe("@msg/#");
        _mqtt.subscribe("@private/shadow/data/get/response");
        _mqtt.subscribe("@shadow/data/get/response");
        _mqtt.subscribe("@shadow/data/updated");
        
        LOG_DEBUG("Subscribed to MQTT topics");
        
        // Request shadow data
        _networkTaskCheckpoint("netpie_shadow_request");
        _mqtt.publish("@shadow/data/get", "{}");
        _shadowRequested = true;
        
        return true;
    } else {
        _networkTaskCheckpoint("netpie_connect_fail");
        int state = _mqtt.state();
        bool cloudUnreachable = (state == MQTT_CONNECT_FAILED);

        if (cloudUnreachable) {
            _cloudConnectFailStreak++;
        } else {
            _cloudConnectFailStreak = 0;
        }

        if (state != _lastConnectErrorState || _lastConnectErrorState == MQTT_CONNECTED) {
            LOG_WARN(
                "MQTT connection failed, rc=%d (%s)",
                state,
                _mqttStateName(state)
            );
        } else {
            LOG_DEBUG(
                "MQTT connection still failing, rc=%d (%s)",
                state,
                _mqttStateName(state)
            );
        }

        if (localMqttIsConnected() && cloudUnreachable &&
            _cloudConnectFailStreak >= NETPIE_CLOUD_UNREACHABLE_FAIL_THRESHOLD) {
            _cloudRetrySuspendedUntil = millis() + NETPIE_CLOUD_UNREACHABLE_COOLDOWN_MS;
            _cloudConnectFailStreak = 0;
            LOG_WARN(
                "NETPIE cloud unreachable while Local MQTT is healthy; pause retries for %lu ms and check internet/NAT/DNS to %s:%d",
                NETPIE_CLOUD_UNREACHABLE_COOLDOWN_MS,
                MQTT_BROKER,
                MQTT_PORT
            );
        }

        _lastConnectErrorState = state;
        return false;
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void netpieSetup(void) {
    LOG_INFO("Initializing NETPIE MQTT...");
    
    // Set TCP socket timeout to 5 seconds (default ~30s causes task stuck detection)
    _wifiClient.setTimeout(5);  // seconds
    
    _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    _mqtt.setCallback(_mqttCallback);
    _mqtt.setBufferSize(NETPIE_MQTT_MESSAGE_BUFFER_SIZE);
    _mqtt.setSocketTimeout(5);  // PubSubClient keepalive timeout (seconds)
    
    LOG_INFO("MQTT Broker: %s:%d", MQTT_BROKER, MQTT_PORT);
}

void netpieLoop(void) {
    if (!wifiIsConnected()) {
        return;  // ไม่ต่อ MQTT ถ้า WiFi ยังไม่เชื่อม
    }

    unsigned long now = millis();

    if (_cloudRetrySuspendedUntil != 0 && !localMqttIsConnected()) {
        _cloudRetrySuspendedUntil = 0;
        _cloudConnectFailStreak = 0;
        LOG_INFO("Local MQTT unavailable; resume NETPIE reconnect attempts immediately");
    }

    if (_cloudRetrySuspended(now)) {
        return;
    }

    if (_cloudRetrySuspendedUntil != 0) {
        _cloudRetrySuspendedUntil = 0;
        LOG_INFO("Retrying NETPIE after cloud-unreachable cooldown");
    }
    
    if (!_mqtt.connected()) {
        unsigned long effectiveReconnectInterval = _reconnectInterval;

        // When the local dashboard path is healthy, avoid letting cloud reconnects stall it.
        if (localMqttIsConnected() && effectiveReconnectInterval < NETPIE_LOCAL_PRIORITY_RETRY_INTERVAL) {
            effectiveReconnectInterval = NETPIE_LOCAL_PRIORITY_RETRY_INTERVAL;
        }

        if (now - _lastReconnectAttempt >= effectiveReconnectInterval) {
            _lastReconnectAttempt = now;
            if (_mqttReconnect()) {
                _lastReconnectAttempt = 0;
                _reconnectInterval = MQTT_RECONNECT_INTERVAL;  // Reset backoff on success
                systemIncrementMqttReconnects();
            } else {
                // Exponential backoff: 5s -> 10s -> 20s -> 30s -> 60s (max)
                _reconnectInterval = min(_reconnectInterval * 2, MAX_NETPIE_RECONNECT_INTERVAL);
                LOG_DEBUG("NETPIE next reconnect in %lu ms", _reconnectInterval);
            }
        }
    } else {
        _networkTaskCheckpoint("netpie_poll");
        _mqtt.loop();
    }
}

bool netpieIsConnected(void) {
    return _mqtt.connected();
}

void netpiePublishData(float waterTemp,
                       float waterTempFish,
                       float airTemp,
                       float humidity,
                       float tds,
                       float tdsFish,
                       float light,
                       float ph) {
    if (millis() - _lastPublishTime < NETPIE_PUBLISH_INTERVAL) {
        return;
    }
    _lastPublishTime = millis();
    
    if (!netpieIsConnected()) {
        return;
    }
    
    // Flush keepalive / detect stale connection before expensive publish
    _networkTaskCheckpoint("netpie_preloop");
    _mqtt.loop();
    if (!_mqtt.connected()) {
        return;
    }
    
    StaticJsonDocument<768> doc;
    JsonObject data = doc.createNestedObject("data");
    
    if (!isnan(waterTemp)) {
        data["water_temp"] = round(waterTemp * 10) / 10.0;
        data["water_temp_mix"] = round(waterTemp * 10) / 10.0;
    }
    if (!isnan(waterTempFish)) {
        data["water_temp_fish"] = round(waterTempFish * 10) / 10.0;
    }
    if (!isnan(airTemp)) {
        data["air_temp"] = round(airTemp * 10) / 10.0;
    }
    if (!isnan(humidity)) {
        data["humidity"] = round(humidity * 10) / 10.0;
    }
    if (tds >= 0) {
        data["tds"] = round(tds * 10) / 10.0;
        data["tds_mix"] = round(tds * 10) / 10.0;
    }
    if (tdsFish >= 0) {
        data["tds_fish"] = round(tdsFish * 10) / 10.0;
    }
    if (light >= 0) {
        data["light"] = round(light * 10) / 10.0;
    }
    if (ph >= 0) {
        data["ph"] = round(ph * 100) / 100.0;  // 2 decimal places for pH
    }
    
    // เพิ่มสถานะไฟ
    data["light_relay"] = lightCtrlGetState() ? 1 : 0;
    data["light_source"] = lightCtrlGetCommandSourceString(lightCtrlGetCommandSource());

    FishFeederConfig feederCfg;
    FishFeederStatus feederStatus;
    fishFeederGetConfig(&feederCfg);
    fishFeederGetStatus(&feederStatus);
    data["feeder_enabled"] = feederCfg.enabled;
    data["feeder_running"] = feederStatus.running;
    data["feeder_state"] = fishFeederGetStateString(feederStatus.state);
    data["feeder_source"] = commandSourceToString(feederCfg.commandSource);
    
    char payload[768];
    serializeJson(doc, payload);
    
    _networkTaskCheckpoint("netpie_publish");
    if (_mqtt.publish("@shadow/data/update", payload)) {
        _networkTaskCheckpoint("netpie_publish_ok");
        LOG_DEBUG("Published to NETPIE: %s", payload);
    } else {
        _networkTaskCheckpoint("netpie_publish_fail");
        LOG_ERROR("NETPIE publish failed!");
    }
}

void netpiePublish(const char* topic, const char* payload) {
    if (!netpieIsConnected()) {
        return;
    }
    _mqtt.publish(topic, payload);
}

bool netpieRequestShadowSync(void) {
    if (!netpieIsConnected()) {
        LOG_WARN("NETPIE shadow sync requested while MQTT is disconnected");
        return false;
    }

    _networkTaskCheckpoint("netpie_shadow_sync");
    bool published = _mqtt.publish("@shadow/data/get", "{}");
    if (published) {
        _shadowRequested = true;
        LOG_INFO("Requested NETPIE shadow sync");
    } else {
        LOG_WARN("Failed to request NETPIE shadow sync");
    }
    return published;
}
