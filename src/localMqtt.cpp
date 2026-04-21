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
#include "automator.h"
#include "TdsSensor.h"  // For TDS calibration
#include "phSensor.h"   // For pH calibration
#include "tempSensor.h" // For HW test sensor reads
#include "dhtSensor.h"  // For HW test sensor reads
#include "lightSensor.h" // For HW test sensor reads
#include "lightController.h" // For HW test light control
#include "fishFeeder.h"
#include "fanController.h"
#include "commandHandler.h"  // For pump test tick
#include "waterSystem.h"


// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static WiFiClient _localWifiClient;
static PubSubClient _localMqtt(_localWifiClient);
static QueueHandle_t _logQueue = NULL;
static unsigned long _lastReconnectAttempt = 0;
static unsigned long _lastPublishTime = 0;
static unsigned long _lastWaterStatusPublishTime = 0;
static unsigned long _lastFanStatusPublishTime = 0;
static unsigned long _lastLightStatusPublishTime = 0;
static unsigned long _lastFeederStatusPublishTime = 0;
static IPAddress _brokerIp;
static bool _isIpResolved = false;
static uint8_t _connectionFailCount = 0;
static uint8_t _statusPublishCursor = 0;
static const uint8_t MAX_FAIL_BEFORE_RERESOLUTION = 3; // Re-resolve mDNS หลังล้มเหลว 3 ครั้ง
static unsigned long _reconnectInterval = 5000;          // Backoff interval (เริ่ม 5s, เพิ่มถึง 60s)
static const unsigned long MAX_RECONNECT_INTERVAL = 60000; // สูงสุด 60 วินาที
static const uint16_t LOCAL_MQTT_PACKET_BUFFER_SIZE = 2048;
static const uint8_t LOCAL_MQTT_MAX_LOGS_PER_LOOP = 1;

// HW Test: deadline-based pump auto-off handled in TaskNetworking tick.
static bool _hwTestPumpActive = false;
static int _hwTestPumpPin = -1;
static unsigned long _hwTestPumpStopAt = 0;
static char _hwTestCompletedCmd[16] = {0};

static void _onMqttMessage(char* topic, byte* payload, unsigned int length);
static bool _reconnect(void);
static void _publishSensorConfig(void);
static void _publishPhCalibrationStatus(void);
static void _publishWaterSystemStatus(void);
static void _publishFanStatus(void);
static void _publishLightStatus(void);
static void _publishFishFeederStatus(void);
static void _serviceScheduledStatusPublishes(void);

static CommandSource _parseCommandSource(const char* sourceValue, CommandSource fallback) {
    if (sourceValue == NULL || sourceValue[0] == '\0') {
        return fallback;
    }

    if (strcmp(sourceValue, "netpie") == 0 || strcmp(sourceValue, "NETPIE") == 0) {
        return COMMAND_SOURCE_NETPIE;
    }
    if (strcmp(sourceValue, "local_web") == 0 || strcmp(sourceValue, "LOCAL_WEB") == 0 ||
        strcmp(sourceValue, "web") == 0 || strcmp(sourceValue, "WEB") == 0 ||
        strcmp(sourceValue, "local") == 0 || strcmp(sourceValue, "LOCAL") == 0) {
        return COMMAND_SOURCE_LOCAL_WEB;
    }

    return fallback;
}

static void _armHwTestPumpAutoOff(uint8_t pin, unsigned long durationMs, const char* cmd) {
    _hwTestPumpPin = pin;
    _hwTestPumpStopAt = millis() + durationMs;
    _hwTestPumpActive = true;
    strncpy(_hwTestCompletedCmd, cmd, sizeof(_hwTestCompletedCmd) - 1);
    _hwTestCompletedCmd[sizeof(_hwTestCompletedCmd) - 1] = '\0';
    LOG_INFO("[HW TEST] Auto-off armed: Pin=%d, Dur=%lu ms", pin, durationMs);
}

static void _stopHwTestPumpOutputs(bool resumeAutomator) {
    digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
    digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);

    _hwTestPumpActive = false;
    _hwTestPumpPin = -1;
    _hwTestPumpStopAt = 0;

    if (resumeAutomator) {
        automatorResume();
    }
}

static void _serviceScheduledStatusPublishes(void) {
    unsigned long now = millis();

    for (uint8_t offset = 0; offset < 4; offset++) {
        uint8_t slot = (_statusPublishCursor + offset) % 4;

        switch (slot) {
            case 0:
                if (now - _lastWaterStatusPublishTime >= LOCAL_PUBLISH_INTERVAL) {
                    _lastWaterStatusPublishTime = now;
                    _publishWaterSystemStatus();
                    _statusPublishCursor = (slot + 1) % 4;
                    return;
                }
                break;

            case 1:
                if (now - _lastLightStatusPublishTime >= LOCAL_PUBLISH_INTERVAL) {
                    _lastLightStatusPublishTime = now;
                    _publishLightStatus();
                    _statusPublishCursor = (slot + 1) % 4;
                    return;
                }
                break;

            case 2:
                if (now - _lastFeederStatusPublishTime >= LOCAL_PUBLISH_INTERVAL) {
                    _lastFeederStatusPublishTime = now;
                    _publishFishFeederStatus();
                    _statusPublishCursor = (slot + 1) % 4;
                    return;
                }
                break;

            case 3:
            default:
                if (now - _lastFanStatusPublishTime >= LOCAL_PUBLISH_INTERVAL) {
                    _lastFanStatusPublishTime = now;
                    _publishFanStatus();
                    _statusPublishCursor = (slot + 1) % 4;
                    return;
                }
                break;
        }
    }
}

static WaterRefillRoute _parseWaterRoute(const char* routeValue, WaterRefillRoute fallback) {
    if (routeValue == NULL || routeValue[0] == '\0') {
        return fallback;
    }

    if (strcmp(routeValue, "auto") == 0 || strcmp(routeValue, "AUTO") == 0) {
        return WATER_REFILL_ROUTE_AUTO;
    }
    if (strcmp(routeValue, "fish_tank") == 0 || strcmp(routeValue, "FISH_TANK") == 0 || strcmp(routeValue, "fish") == 0) {
        return WATER_REFILL_ROUTE_FISH_TANK;
    }
    if (strcmp(routeValue, "sump_direct") == 0 || strcmp(routeValue, "SUMP_DIRECT") == 0 || strcmp(routeValue, "sump") == 0) {
        return WATER_REFILL_ROUTE_SUMP_DIRECT;
    }

    return fallback;
}

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
        
        if (strcmp(action, "cal686") == 0 || strcmp(action, "cal7") == 0) {
            if (phIsReady()) {
                phCalibratePh686();
                LOG_INFO("pH 6.86 Calibration triggered from Pi Dashboard!");
                _publishPhCalibrationStatus();
            } else {
                LOG_ERROR("pH sensor not ready for calibration");
            }
        } else if (strcmp(action, "cal401") == 0 || strcmp(action, "cal4") == 0) {
            if (phIsReady()) {
                phCalibratePh401();
                LOG_INFO("pH 4.01 Calibration triggered from Pi Dashboard!");
                _publishPhCalibrationStatus();
            } else {
                LOG_ERROR("pH sensor not ready for calibration");
            }
        } else if (strcmp(action, "cal918") == 0) {
            if (phIsReady()) {
                phCalibratePh918();
                LOG_INFO("pH 9.18 Calibration triggered from Pi Dashboard!");
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
    
    // Handle Automation Target Config
    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_AUTOMATION) == 0) {
        bool enabled = doc["enabled"] | false;
        float tgtTds = doc["target_tds"] | AUTOMATOR_DEFAULT_TDS;
        float tgtPh  = doc["target_ph"] | AUTOMATOR_DEFAULT_PH;
        
        automatorSetConfig(enabled, tgtTds, tgtPh);
        LOG_INFO("Automation Config updated: En=%d, TDS=%.1f, pH=%.1f", enabled, tgtTds, tgtPh);
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_LIGHT_CONTROL) == 0) {
        LightControlConfig cfg;
        lightCtrlGetConfig(&cfg);
        CommandSource previousSource = cfg.commandSource;
        bool switchedToNetpie = false;

        if (doc.containsKey("command_source")) {
            lightCtrlSetCommandSource(_parseCommandSource(doc["command_source"] | "", cfg.commandSource));
            lightCtrlGetConfig(&cfg);
            switchedToNetpie = (previousSource != cfg.commandSource && cfg.commandSource == COMMAND_SOURCE_NETPIE);
        }

        if (lightCtrlAllowsLocalControl()) {
            bool enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : cfg.enabled;
            bool manualState = doc.containsKey("manual_state") ? doc["manual_state"].as<bool>() : cfg.manualState;
            int onDay = doc.containsKey("on_day") ? doc["on_day"].as<int>() : cfg.onDay;
            int offDay = doc.containsKey("off_day") ? doc["off_day"].as<int>() : cfg.offDay;
            const char* onTime = doc.containsKey("on_time") ? doc["on_time"] | cfg.onTime : cfg.onTime;
            const char* offTime = doc.containsKey("off_time") ? doc["off_time"] | cfg.offTime : cfg.offTime;

            lightCtrlSetEnabled(enabled ? 1 : 0);
            lightCtrlSetManualState(manualState);
            lightCtrlSetOnDay(onDay);
            lightCtrlSetOnTime(onTime);
            lightCtrlSetOffDay(offDay);
            lightCtrlSetOffTime(offTime);
            LOG_INFO("Light config updated from Local Web: Enabled=%d, Manual=%d, On=%d %s, Off=%d %s",
                     enabled, manualState, onDay, onTime, offDay, offTime);
        } else {
            LOG_WARN("Local Web light config ignored because control source is not local_web");
        }

        if (switchedToNetpie) {
            if (!netpieRequestShadowSync()) {
                LOG_WARN("Switched light source to NETPIE but could not request shadow refresh yet");
            }
        }

        _publishLightStatus();
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_FISH_FEEDER) == 0) {
        FishFeederConfig cfg;
        fishFeederGetConfig(&cfg);

        if (doc.containsKey("command_source")) {
            fishFeederSetCommandSource(_parseCommandSource(doc["command_source"] | "", cfg.commandSource));
            fishFeederGetConfig(&cfg);
        }

        if (fishFeederAllowsLocalControl()) {
            bool enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : cfg.enabled;
            int feedDay = doc.containsKey("feed_day") ? doc["feed_day"].as<int>() : cfg.feedDay;
            const char* feedTime = doc.containsKey("feed_time") ? doc["feed_time"] | cfg.feedTime : cfg.feedTime;
            unsigned long durationMs = doc.containsKey("duration_ms") ? doc["duration_ms"].as<unsigned long>() : cfg.durationMs;

            fishFeederSetEnabled(enabled);
            fishFeederSetFeedDay(feedDay);
            fishFeederSetFeedTime(feedTime);
            fishFeederSetDurationMs(durationMs);

            if (doc["trigger_feed"] | false) {
                fishFeederStartManualFeed("Manual feed triggered from Local Web");
            }

            LOG_INFO("Fish feeder config updated from Local Web: Enabled=%d, Day=%d, Time=%s, Duration=%lu",
                     enabled, feedDay, feedTime, durationMs);
        } else {
            LOG_WARN("Local Web fish feeder config ignored because control source is not local_web");
        }

        _publishFishFeederStatus();
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_FAN_CONTROL) == 0) {
        FanControlConfig cfg;
        fanCtrlGetConfig(&cfg);

        bool enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : cfg.enabled;
        bool autoMode = doc.containsKey("auto_mode") ? doc["auto_mode"].as<bool>() : cfg.autoMode;
        bool manualState = doc.containsKey("manual_state") ? doc["manual_state"].as<bool>() : cfg.manualState;
        float tempOnC = doc.containsKey("temp_on_c") ? doc["temp_on_c"].as<float>() : cfg.tempOnC;
        float tempOffC = doc.containsKey("temp_off_c") ? doc["temp_off_c"].as<float>() : cfg.tempOffC;
        float humidityOnPct = doc.containsKey("humidity_on_pct") ? doc["humidity_on_pct"].as<float>() : cfg.humidityOnPct;
        float humidityOffPct = doc.containsKey("humidity_off_pct") ? doc["humidity_off_pct"].as<float>() : cfg.humidityOffPct;

        fanCtrlSetConfig(enabled, autoMode, manualState, tempOnC, tempOffC, humidityOnPct, humidityOffPct);
        LOG_INFO("Fan Config updated: En=%d, Auto=%d, Manual=%d, T=%.1f/%.1f, H=%.1f/%.1f",
                 enabled, autoMode, manualState, tempOnC, tempOffC, humidityOnPct, humidityOffPct);
        _publishFanStatus();
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_WATER_SYSTEM) == 0) {
        WaterSystemConfig cfg;
        waterSystemGetConfig(&cfg);

        bool circulationEnabled = doc.containsKey("circulation_enabled") ? doc["circulation_enabled"].as<bool>() : cfg.circulationEnabled;
        bool refillEnabled = doc.containsKey("refill_enabled") ? doc["refill_enabled"].as<bool>() : cfg.refillEnabled;
        unsigned long refillMaxRuntimeMs = doc.containsKey("refill_max_runtime_ms") ? doc["refill_max_runtime_ms"].as<unsigned long>() : cfg.refillMaxRuntimeMs;
        WaterRefillRoute preferredRoute = cfg.preferredRoute;
        bool allowDirectSumpRefill = doc.containsKey("allow_direct_sump_refill") ? doc["allow_direct_sump_refill"].as<bool>() : cfg.allowDirectSumpRefill;

        if (doc.containsKey("preferred_route")) {
            preferredRoute = _parseWaterRoute(doc["preferred_route"] | "", cfg.preferredRoute);
        }

        waterSystemSetConfig(circulationEnabled, refillEnabled, refillMaxRuntimeMs, preferredRoute, allowDirectSumpRefill);

        if (doc.containsKey("manual_refill")) {
            waterSystemSetManualRefill(doc["manual_refill"].as<bool>());
        }

        if (doc["clear_alarm"] | false) {
            waterSystemClearAlarm();
        }

        LOG_INFO("Water System config updated: Circ=%d, Refill=%d, Route=%s, Direct=%d, Max=%lu ms",
                 circulationEnabled,
                 refillEnabled,
                 waterSystemGetRouteString(preferredRoute),
                 allowDirectSumpRefill,
                 refillMaxRuntimeMs);
        _publishWaterSystemStatus();
    }
    
    // Handle Hardware Test Commands (Pi Dashboard → ESP32)
    if (strcmp(topic, LOCAL_MQTT_TOPIC_HW_TEST_CMD) == 0) {
        const char* cmd = doc["cmd"] | "";
        unsigned long duration = doc["duration"] | HW_TEST_PUMP_DURATION_MS;
        if (duration <= 0) duration = HW_TEST_PUMP_DURATION_MS;
        
        StaticJsonDocument<256> result;
        result["cmd"] = cmd;
        
        // Pause automator during pump testing to prevent interference
        if (strncmp(cmd, "pump_", 5) == 0) {
            automatorPause();
        }
        
        if (strcmp(cmd, "pump_a") == 0) {
            _stopHwTestPumpOutputs(false);
            digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_ON);
            result["status"] = "running";
            result["duration_ms"] = duration;
            result["gpio"] = PUMP_NUTRIENT_A_PIN;
            LOG_INFO("[HW TEST] Pump A ON for %d ms", duration);
        }
        else if (strcmp(cmd, "pump_b") == 0) {
            _stopHwTestPumpOutputs(false);
            digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_ON);
            result["status"] = "running";
            result["duration_ms"] = duration;
            result["gpio"] = PUMP_NUTRIENT_B_PIN;
            LOG_INFO("[HW TEST] Pump B ON for %d ms", duration);
        }

        else if (strcmp(cmd, "pump_stop") == 0) {
            _stopHwTestPumpOutputs(true);
            result["status"] = "stopped";
            LOG_INFO("[HW TEST] All pumps STOPPED, automator resumed");
        }
        else if (strcmp(cmd, "light_on") == 0) {
            lightCtrlSetState(true);
            result["status"] = "on";
            LOG_INFO("[HW TEST] Light ON");
        }
        else if (strcmp(cmd, "light_off") == 0) {
            lightCtrlSetState(false);
            result["status"] = "off";
            LOG_INFO("[HW TEST] Light OFF");
        }
        else if (strcmp(cmd, "feeder_feed") == 0) {
            fishFeederSetDurationMs(duration);
            if (fishFeederStartManualFeed("Hardware test feed triggered")) {
                result["status"] = "feeding";
                result["duration_ms"] = duration;
                result["gpio"] = FISH_FEEDER_PIN;
                LOG_INFO("[HW TEST] Fish feeder ON for %lu ms", duration);
                _publishFishFeederStatus();
            } else {
                result["status"] = "error";
                result["message"] = "Feeder unavailable";
            }
        }
        else if (strcmp(cmd, "fan_on") == 0) {
            fanCtrlSetEnabled(true);
            fanCtrlSetManualState(true);
            result["status"] = "on";
            LOG_INFO("[HW TEST] Fan manual ON");
            _publishFanStatus();
        }
        else if (strcmp(cmd, "fan_off") == 0) {
            fanCtrlSetEnabled(true);
            fanCtrlSetManualState(false);
            result["status"] = "off";
            LOG_INFO("[HW TEST] Fan manual OFF");
            _publishFanStatus();
        }
        else if (strcmp(cmd, "fan_auto") == 0) {
            fanCtrlSetEnabled(true);
            fanCtrlSetAutoMode(true);
            result["status"] = "auto";
            LOG_INFO("[HW TEST] Fan AUTO mode");
            _publishFanStatus();
        }
        else if (strcmp(cmd, "read_sensors") == 0) {
            result["status"] = "ok";
            float wt = tempRead();
            float at = dhtReadTemperature();
            float hm = dhtReadHumidity();
            float td = tdsRead(wt);
            float ph = phRead();
            float lx = lightRead();
            if (!isnan(wt)) result["water_temp"] = serialized(String(wt, 1));
            if (!isnan(at)) result["air_temp"] = serialized(String(at, 1));
            if (!isnan(hm)) result["humidity"] = serialized(String(hm, 1));
            if (td >= 0) result["tds"] = serialized(String(td, 0));
            if (ph >= 0) result["ph"] = serialized(String(ph, 2));
            if (lx >= 0) result["light"] = serialized(String(lx, 0));
            LOG_INFO("[HW TEST] Sensors read complete");
        }
        else {
            result["status"] = "error";
            result["message"] = "Unknown command";
            LOG_WARN("[HW TEST] Unknown command: %s", cmd);
        }
        
        // Publish result back
        char buf[256];
        serializeJson(result, buf, sizeof(buf));
        _localMqtt.publish(LOCAL_MQTT_TOPIC_HW_TEST_RESULT, buf);
        
        // Schedule auto-off using one-shot FreeRTOS task
        if (strncmp(cmd, "pump_", 5) == 0 && strcmp(cmd, "pump_stop") != 0) {
            uint8_t pin = 0;
            if (strcmp(cmd, "pump_a") == 0) pin = PUMP_NUTRIENT_A_PIN;
            else if (strcmp(cmd, "pump_b") == 0) pin = PUMP_NUTRIENT_B_PIN;

            
            if (pin > 0) {
                _armHwTestPumpAutoOff(pin, duration, cmd);
            }
        }
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
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_CONFIG_AUTOMATION, 1);
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_CONFIG_LIGHT_CONTROL, 1);
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_CONFIG_FISH_FEEDER, 1);
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_CONFIG_FAN_CONTROL, 1);
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_CONFIG_WATER_SYSTEM, 1);
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_HW_TEST_CMD, 1);
        LOG_INFO("Subscribed to MQTT topics (QoS 1)");

        _publishLightStatus();
        _publishFishFeederStatus();
        _publishFanStatus();
        _publishWaterSystemStatus();
        
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
    
    _localMqtt.setBufferSize(LOCAL_MQTT_PACKET_BUFFER_SIZE);
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
        _serviceScheduledStatusPublishes();
        
        // Process cross-core log queue safely in the Networking Task
        if (_logQueue != NULL) {
            char logBuff[128];
            int count = 0;
            // Process a small number of logs per loop to avoid long publish bursts.
            while (count < LOCAL_MQTT_MAX_LOGS_PER_LOOP && xQueueReceive(_logQueue, logBuff, 0) == pdTRUE) {
                _localMqtt.publish(LOCAL_MQTT_TOPIC_LOGS, logBuff);
                count++;
            }
        }
        
    }
}

/**
 * @brief HW Test tick — publish completed + resume automator (thread-safe in TaskNetworking)
 * @note Called from main.cpp TaskNetworking every loop iteration
 */
void localMqttHwTestTick(void) {
    if (!_hwTestPumpActive) return;

    if ((long)(millis() - _hwTestPumpStopAt) < 0) {
        return;
    }

    if (_hwTestPumpPin >= 0) {
        digitalWrite(_hwTestPumpPin, PUMP_OFF);
        LOG_INFO("[HW TEST] Auto-off: Pin %d -> OFF", _hwTestPumpPin);
    }

    _hwTestPumpActive = false;
    _hwTestPumpPin = -1;
    _hwTestPumpStopAt = 0;

    // Resume automator (safe from Core 0, automator checks volatile flag)
    automatorResume();

    // Publish completed via MQTT (thread-safe: same task as PubSubClient)
    if (_localMqtt.connected()) {
        StaticJsonDocument<128> result;
        result["cmd"] = _hwTestCompletedCmd;
        result["status"] = "completed";
        char buf[128];
        serializeJson(result, buf, sizeof(buf));
        _localMqtt.publish(LOCAL_MQTT_TOPIC_HW_TEST_RESULT, buf);
        LOG_INFO("[HW TEST] Published 'completed' for %s", _hwTestCompletedCmd);
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

    StaticJsonDocument<1536> doc;
    
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
    
    // Add Automator Process State (Process Tracker)
    AutomatorConfig authCfg;
    automatorGetConfig(&authCfg);
    doc["auto_enabled"] = authCfg.enabled;
    doc["auto_tgt_tds"] = authCfg.targetTds;
    doc["auto_tgt_ph"] = authCfg.targetPh;
    doc["auto_state"] = automatorGetStateString(automatorGetCurrentState());
    doc["auto_next_state"] = automatorGetNextStateString();
    doc["auto_reason"] = automatorGetActionReason();
    doc["auto_time_left"] = automatorGetTimeRemainingSec();

    LightControlConfig lightCfg;
    LightControlStatus lightStatus;
    lightCtrlGetConfig(&lightCfg);
    lightCtrlGetStatus(&lightStatus);
    doc["light_relay"] = lightStatus.running;
    doc["light_schedule_enabled"] = lightCfg.enabled;
    doc["light_manual_state"] = lightCfg.manualState;
    doc["light_source"] = lightCtrlGetCommandSourceString(lightCfg.commandSource);

    FanControlConfig fanCfg;
    FanControlStatus fanStatus;
    fanCtrlGetConfig(&fanCfg);
    fanCtrlGetStatus(&fanStatus);
    doc["fan_enabled"] = fanCfg.enabled;
    doc["fan_auto_mode"] = fanCfg.autoMode;
    doc["fan_manual_state"] = fanCfg.manualState;
    doc["fan_running"] = fanStatus.running;
    doc["fan_state"] = fanCtrlGetStateString(fanStatus.state);
    doc["fan_reason"] = fanStatus.reason;
    doc["fan_has_output"] = fanStatus.hasOutput;

    FishFeederConfig feederCfg;
    FishFeederStatus feederStatus;
    fishFeederGetConfig(&feederCfg);
    fishFeederGetStatus(&feederStatus);
    doc["feeder_enabled"] = feederCfg.enabled;
    doc["feeder_running"] = feederStatus.running;
    doc["feeder_state"] = fishFeederGetStateString(feederStatus.state);
    doc["feeder_source"] = commandSourceToString(feederCfg.commandSource);

    WaterSystemConfig waterCfg;
    WaterSystemStatus waterStatus;
    waterSystemGetConfig(&waterCfg);
    waterSystemGetStatus(&waterStatus);
    doc["circ_enabled"] = waterCfg.circulationEnabled;
    doc["circ_running"] = waterStatus.circulationOutput;
    doc["refill_enabled"] = waterCfg.refillEnabled;
    doc["refill_running"] = waterStatus.refillOutput;
    doc["manual_refill"] = waterCfg.manualRefill;
    doc["preferred_route"] = waterSystemGetRouteString(waterCfg.preferredRoute);
    doc["active_route"] = waterSystemGetRouteString(waterStatus.activeRoute);
    doc["allow_direct_sump_refill"] = waterCfg.allowDirectSumpRefill;
    doc["route_blocked"] = waterStatus.routeBlocked;
    doc["sump_low"] = waterStatus.levelLow;
    doc["sump_high"] = waterStatus.levelHigh;
    doc["fish_overflow"] = waterStatus.overflowAlarm;
    doc["water_alarm"] = waterStatus.alarmActive;
    doc["water_state"] = waterSystemGetStateString(waterStatus.state);
    doc["water_reason"] = waterStatus.reason;

    char payload[LOCAL_MQTT_PACKET_BUFFER_SIZE];
    size_t payloadLen = serializeJson(doc, payload, sizeof(payload));

    if (payloadLen == 0 || payloadLen >= sizeof(payload) - 1) {
        LOG_ERROR("Local MQTT payload too large (%u bytes), skipping publish", (unsigned int)payloadLen);
        return;
    }

    if (_localMqtt.publish(LOCAL_MQTT_TOPIC_SENSORS, payload)) {
        LOG_DEBUG("Local MQTT Publish: %s", payload);
    } else {
        LOG_ERROR("Local MQTT Publish Failed (len=%u, state=%d)",
                  (unsigned int)payloadLen,
                  _localMqtt.state());
    }
}

void localMqttPublishLog(const char* logMsg) {
    // Skip if no network or MQTT not connected — don't fill queue needlessly
    if (!wifiIsConnected() || !_localMqtt.connected()) return;
    
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
    doc["calibrated"] = phHasCalibration401() || phHasCalibration686() || phHasCalibration918();
    doc["cal401_done"] = phHasCalibration401();
    doc["cal686_done"] = phHasCalibration686();
    doc["cal918_done"] = phHasCalibration918();
    
    char buffer[256];
    serializeJson(doc, buffer);
    _localMqtt.publish("aquaponics/status/ph_cal", buffer);
    LOG_INFO("Sent pH Calibration Status: %s", buffer);
}

static void _publishWaterSystemStatus(void) {
    if (!_localMqtt.connected()) return;

    WaterSystemConfig cfg;
    WaterSystemStatus status;
    waterSystemGetConfig(&cfg);
    waterSystemGetStatus(&status);

    StaticJsonDocument<384> doc;
    doc["circulation_enabled"] = cfg.circulationEnabled;
    doc["refill_enabled"] = cfg.refillEnabled;
    doc["manual_refill"] = cfg.manualRefill;
    doc["refill_max_runtime_ms"] = cfg.refillMaxRuntimeMs;
    doc["preferred_route"] = waterSystemGetRouteString(cfg.preferredRoute);
    doc["allow_direct_sump_refill"] = cfg.allowDirectSumpRefill;
    doc["state"] = waterSystemGetStateString(status.state);
    doc["reason"] = status.reason;
    doc["circulation_output"] = status.circulationOutput;
    doc["refill_output"] = status.refillOutput;
    doc["active_route"] = waterSystemGetRouteString(status.activeRoute);
    doc["route_valve_output"] = status.routeValveOutput;
    doc["sump_low"] = status.levelLow;
    doc["sump_high"] = status.levelHigh;
    doc["overflow_alarm"] = status.overflowAlarm;
    doc["alarm_active"] = status.alarmActive;
    doc["has_circulation_pump"] = status.hasCirculationPump;
    doc["has_refill_pump"] = status.hasRefillPump;
    doc["has_level_sensors"] = status.hasLevelSensors;
    doc["has_overflow_sensor"] = status.hasOverflowSensor;
    doc["has_route_valve"] = status.hasRouteValve;
    doc["route_blocked"] = status.routeBlocked;

    char buffer[384];
    serializeJson(doc, buffer, sizeof(buffer));
    _localMqtt.publish(LOCAL_MQTT_TOPIC_STATUS_WATER_SYSTEM, buffer);
}

static void _publishFanStatus(void) {
    if (!_localMqtt.connected()) return;

    FanControlConfig cfg;
    FanControlStatus status;
    fanCtrlGetConfig(&cfg);
    fanCtrlGetStatus(&status);

    StaticJsonDocument<320> doc;
    doc["enabled"] = cfg.enabled;
    doc["auto_mode"] = cfg.autoMode;
    doc["manual_state"] = cfg.manualState;
    doc["temp_on_c"] = round(cfg.tempOnC * 10) / 10.0;
    doc["temp_off_c"] = round(cfg.tempOffC * 10) / 10.0;
    doc["humidity_on_pct"] = round(cfg.humidityOnPct * 10) / 10.0;
    doc["humidity_off_pct"] = round(cfg.humidityOffPct * 10) / 10.0;
    doc["state"] = fanCtrlGetStateString(status.state);
    doc["running"] = status.running;
    doc["has_output"] = status.hasOutput;
    doc["reason"] = status.reason;
    if (!isnan(status.airTempC)) doc["air_temp_c"] = round(status.airTempC * 10) / 10.0;
    if (!isnan(status.humidityPct)) doc["humidity_pct"] = round(status.humidityPct * 10) / 10.0;

    char buffer[320];
    serializeJson(doc, buffer, sizeof(buffer));
    _localMqtt.publish(LOCAL_MQTT_TOPIC_STATUS_FAN_CONTROL, buffer);
}

static void _publishLightStatus(void) {
    if (!_localMqtt.connected()) return;

    LightControlConfig cfg;
    LightControlStatus status;
    lightCtrlGetConfig(&cfg);
    lightCtrlGetStatus(&status);

    StaticJsonDocument<320> doc;
    doc["command_source"] = lightCtrlGetCommandSourceString(cfg.commandSource);
    doc["enabled"] = cfg.enabled;
    doc["manual_state"] = cfg.manualState;
    doc["on_day"] = cfg.onDay;
    doc["on_time"] = cfg.onTime;
    doc["off_day"] = cfg.offDay;
    doc["off_time"] = cfg.offTime;
    doc["running"] = status.running;
    doc["ntp_synced"] = status.ntpSynced;
    doc["has_output"] = status.hasOutput;
    doc["reason"] = status.reason;

    char buffer[320];
    serializeJson(doc, buffer, sizeof(buffer));
    _localMqtt.publish(LOCAL_MQTT_TOPIC_STATUS_LIGHT_CONTROL, buffer);
}

static void _publishFishFeederStatus(void) {
    if (!_localMqtt.connected()) return;

    FishFeederConfig cfg;
    FishFeederStatus status;
    fishFeederGetConfig(&cfg);
    fishFeederGetStatus(&status);

    StaticJsonDocument<320> doc;
    doc["command_source"] = commandSourceToString(cfg.commandSource);
    doc["enabled"] = cfg.enabled;
    doc["feed_day"] = cfg.feedDay;
    doc["feed_time"] = cfg.feedTime;
    doc["duration_ms"] = cfg.durationMs;
    doc["state"] = fishFeederGetStateString(status.state);
    doc["running"] = status.running;
    doc["has_output"] = status.hasOutput;
    doc["last_feed_at"] = status.lastFeedAt;
    doc["reason"] = status.reason;

    char buffer[320];
    serializeJson(doc, buffer, sizeof(buffer));
    _localMqtt.publish(LOCAL_MQTT_TOPIC_STATUS_FISH_FEEDER, buffer);
}


