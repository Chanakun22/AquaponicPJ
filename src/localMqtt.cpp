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
#include "commandHandler.h"  // For pump test tick


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

// HW Test: one-shot FreeRTOS task for guaranteed pump auto-off
static TaskHandle_t _hwTestTaskHandle = NULL;
static volatile bool _hwTestCompleted = false;  // Flag: task finished, tick should publish
static char _hwTestCompletedCmd[16] = {0};

struct HwTestParams {
    uint8_t pin;
    unsigned long durationMs;
    char cmd[16];
};

/**
 * @brief One-shot task: wait duration, turn off pump, set flag, self-delete
 * @note Only does digitalWrite (thread-safe). MQTT publish done in tick.
 */
static void _hwTestAutoOffTask(void* param) {
    HwTestParams* p = (HwTestParams*)param;
    uint8_t pin = p->pin;
    unsigned long dur = p->durationMs;
    strncpy(_hwTestCompletedCmd, p->cmd, sizeof(_hwTestCompletedCmd) - 1);
    delete p;
    
    LOG_INFO("[HW TEST] Auto-off task started: Pin=%d, Wait=%lu ms", pin, dur);
    
    // Wait for the duration (blocking in own task = safe)
    vTaskDelay(pdMS_TO_TICKS(dur));
    
    // TURN OFF PUMP — this is just a register write, thread-safe
    digitalWrite(pin, PUMP_OFF);
    LOG_INFO("[HW TEST] Auto-off task: Pin %d -> OFF", pin);
    
    // Set flag for tick to handle MQTT publish + automator resume
    _hwTestCompleted = true;
    
    _hwTestTaskHandle = NULL;
    vTaskDelete(NULL);
}

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
    
    // Handle Automation Target Config
    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_AUTOMATION) == 0) {
        bool enabled = doc["enabled"] | false;
        float tgtTds = doc["target_tds"] | AUTOMATOR_DEFAULT_TDS;
        float tgtPh  = doc["target_ph"] | AUTOMATOR_DEFAULT_PH;
        
        automatorSetConfig(enabled, tgtTds, tgtPh);
        LOG_INFO("Automation Config updated: En=%d, TDS=%.1f, pH=%.1f", enabled, tgtTds, tgtPh);
    }
    
    // Handle Hardware Test Commands (Pi Dashboard → ESP32)
    if (strcmp(topic, LOCAL_MQTT_TOPIC_HW_TEST_CMD) == 0) {
        const char* cmd = doc["cmd"] | "";
        unsigned long duration = doc["duration"] | 3000;
        if (duration <= 0) duration = 3000;
        
        StaticJsonDocument<256> result;
        result["cmd"] = cmd;
        
        // Pause automator during pump testing to prevent interference
        if (strncmp(cmd, "pump_", 5) == 0) {
            automatorPause();
        }
        
        if (strcmp(cmd, "pump_a") == 0) {
            digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_ON);
            result["status"] = "running";
            result["duration_ms"] = duration;
            result["gpio"] = PUMP_NUTRIENT_A_PIN;
            LOG_INFO("[HW TEST] Pump A ON for %d ms", duration);
        }
        else if (strcmp(cmd, "pump_b") == 0) {
            digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_ON);
            result["status"] = "running";
            result["duration_ms"] = duration;
            result["gpio"] = PUMP_NUTRIENT_B_PIN;
            LOG_INFO("[HW TEST] Pump B ON for %d ms", duration);
        }

        else if (strcmp(cmd, "pump_stop") == 0) {
            digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
            digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);
            automatorResume();  // Resume automator after emergency stop
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
                // Kill previous auto-off task if running
                if (_hwTestTaskHandle != NULL) {
                    vTaskDelete(_hwTestTaskHandle);
                    _hwTestTaskHandle = NULL;
                }
                
                // Allocate params for the task
                HwTestParams* params = new HwTestParams();
                params->pin = pin;
                params->durationMs = duration;
                strncpy(params->cmd, cmd, sizeof(params->cmd) - 1);
                
                // Create one-shot auto-off task (runs on any core)
                BaseType_t ok = xTaskCreate(
                    _hwTestAutoOffTask,
                    "hwTestOff",
                    4096,
                    (void*)params,
                    2,   // Priority 2 (above normal)
                    &_hwTestTaskHandle
                );
                
                if (ok == pdPASS) {
                    LOG_INFO("[HW TEST] Auto-off task created: Pin=%d, Dur=%lu ms", pin, duration);
                } else {
                    LOG_ERROR("[HW TEST] xTaskCreate FAILED!");
                    delete params;
                    digitalWrite(pin, PUMP_OFF);
                }
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
        _localMqtt.subscribe(LOCAL_MQTT_TOPIC_HW_TEST_CMD, 1);
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

/**
 * @brief HW Test tick — publish completed + resume automator (thread-safe in TaskNetworking)
 * @note Called from main.cpp TaskNetworking every loop iteration
 */
void localMqttHwTestTick(void) {
    if (!_hwTestCompleted) return;
    _hwTestCompleted = false;
    
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
    
    // Add Automator Process State (Process Tracker)
    AutomatorConfig authCfg;
    automatorGetConfig(&authCfg);
    doc["auto_enabled"] = authCfg.enabled;
    doc["auto_tgt_tds"] = authCfg.targetTds;
    doc["auto_tgt_ph"] = authCfg.targetPh;
    doc["auto_state"] = automatorGetStateString(automatorGetCurrentState());
    doc["auto_reason"] = automatorGetActionReason();
    doc["auto_time_left"] = automatorGetTimeRemainingSec();

    char payload[512];
    serializeJson(doc, payload);

    if (_localMqtt.publish(LOCAL_MQTT_TOPIC_SENSORS, payload)) {
        LOG_DEBUG("Local MQTT Publish: %s", payload);
    } else {
        LOG_ERROR("Local MQTT Publish Failed");
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
    doc["calibrated"] = true;
    
    char buffer[256];
    serializeJson(doc, buffer);
    _localMqtt.publish("aquaponics/status/ph_cal", buffer);
    LOG_INFO("Sent pH Calibration Status: %s", buffer);
}


