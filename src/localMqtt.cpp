/**
 * @file localMqtt.cpp
 * @brief Implementation of Local MQTT for Raspberry Pi
 */

#include "localMqtt.h"
#include "config.h"
#include "logger.h"
#include "system.h"
#include "wifiConn.h"
#include "gpioOut.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#if defined(ESP32) && WATCHDOG_ENABLED
#include "esp_task_wdt.h"
#endif
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
static QueueHandle_t _deferredActionQueue = NULL;
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
static const uint16_t LOCAL_MQTT_PACKET_BUFFER_SIZE = 4096;
static const uint8_t LOCAL_MQTT_MAX_LOGS_PER_LOOP = 1;
static StaticJsonDocument<3072> _sensorPublishDoc;
static char _sensorPublishPayload[LOCAL_MQTT_PACKET_BUFFER_SIZE];
static volatile uint8_t _pendingStatusPublishes = 0;
static volatile uint8_t _pendingNetworkRequests = 0;
static portMUX_TYPE _pendingWorkMux = portMUX_INITIALIZER_UNLOCKED;

static const uint8_t LOCAL_MQTT_MAX_DEFERRED_ACTIONS_PER_LOOP = 1;
static const uint8_t LOCAL_MQTT_DEFERRED_QUEUE_LENGTH = 12;

enum LocalStatusPublishMask : uint8_t {
    LOCAL_STATUS_PUBLISH_SENSOR_CONFIG = 1 << 0,
    LOCAL_STATUS_PUBLISH_PH_CAL = 1 << 1,
    LOCAL_STATUS_PUBLISH_WATER = 1 << 2,
    LOCAL_STATUS_PUBLISH_FAN = 1 << 3,
    LOCAL_STATUS_PUBLISH_LIGHT = 1 << 4,
    LOCAL_STATUS_PUBLISH_FEEDER = 1 << 5,
};

enum LocalNetworkRequestMask : uint8_t {
    LOCAL_NETWORK_REQUEST_NETPIE_SHADOW_SYNC = 1 << 0,
};

enum LocalDeferredActionType : uint8_t {
    LOCAL_DEFERRED_TDS_CAL = 0,
    LOCAL_DEFERRED_PH_CAL,
    LOCAL_DEFERRED_SENSOR_CONFIG,
    LOCAL_DEFERRED_AUTOMATION_CONFIG,
    LOCAL_DEFERRED_LIGHT_CONFIG,
    LOCAL_DEFERRED_FEEDER_CONFIG,
    LOCAL_DEFERRED_FAN_CONFIG,
    LOCAL_DEFERRED_WATER_CONFIG,
};

enum LocalPhCalibrationAction : uint8_t {
    LOCAL_PH_CAL_686 = 0,
    LOCAL_PH_CAL_401,
    LOCAL_PH_CAL_918,
    LOCAL_PH_CAL_CLEAR,
};

typedef struct {
    uint8_t type;
    union {
        struct {
            uint8_t channel;    // TdsChannel: 0 = mix, 1 = fish
            float lowPpm;
            float lowVoltage;
            float lowTemperature;
            float highPpm;
            float highVoltage;
            float highTemperature;
            bool rawVoltage;
        } tdsCal;
        struct {
            uint8_t action;
            uint8_t channel;    // PhChannel: 0 = mix, 1 = fish
        } phCal;
        struct {
            bool states[SENSOR_COUNT];
        } sensors;
        struct {
            bool enabled;
            float targetTds;
            float doseAVolumeMl;
            float doseBVolumeMl;
            unsigned long mixAfterAMs;
            unsigned long postDoseMixMs;
            float tdsHysteresisPpm;
        } automation;
        struct {
            CommandSource source;
            bool enabled;
            bool manualState;
            int onDay;
            int offDay;
            char onTime[6];
            char offTime[6];
            bool requestNetpieShadowSync;
        } light;
        struct {
            CommandSource source;
            bool enabled;
            int feedDay;
            char feedTime[6];
            unsigned long durationMs;
            bool triggerFeed;
        } feeder;
        struct {
            bool enabled;
            bool autoMode;
            bool manualState;
            float tempOnC;
            float tempOffC;
            float humidityOnPct;
            float humidityOffPct;
        } fan;
        struct {
            bool circulationEnabled;
            bool refillEnabled;
            unsigned long refillMaxRuntimeMs;
            unsigned long refillMinIntervalMs;
            WaterRefillRoute preferredRoute;
            bool allowDirectSumpRefill;
            unsigned long fishRefillIntervalMs;
            unsigned long fishRefillMaxRuntimeMs;
            bool manualRefill;
            bool applyManualRefill;
            bool clearAlarm;
        } water;
    } data;
} LocalDeferredAction;

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
static bool _enqueueDeferredAction(const LocalDeferredAction* action);
static void _queueNetworkRequest(uint8_t mask);
static bool _takePendingStatusPublish(uint8_t mask);
static bool _takePendingNetworkRequest(uint8_t mask);
static void _processDeferredAction(const LocalDeferredAction* action);

static void _networkTaskCheckpoint(const char* stage) {
    systemSetTaskProgress(TASK_NETWORKING, stage);
    systemTaskHeartbeat(TASK_NETWORKING);

    #if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_reset();
    #endif
}

static void _queueStatusPublish(uint8_t mask) {
    portENTER_CRITICAL(&_pendingWorkMux);
    _pendingStatusPublishes |= mask;
    portEXIT_CRITICAL(&_pendingWorkMux);
}

static void _queueNetworkRequest(uint8_t mask) {
    portENTER_CRITICAL(&_pendingWorkMux);
    _pendingNetworkRequests |= mask;
    portEXIT_CRITICAL(&_pendingWorkMux);
}

static void _queueReconnectStatusPublishes(void) {
    _queueStatusPublish(
        LOCAL_STATUS_PUBLISH_LIGHT |
        LOCAL_STATUS_PUBLISH_FEEDER |
        LOCAL_STATUS_PUBLISH_FAN |
        LOCAL_STATUS_PUBLISH_WATER
    );
}

static bool _takePendingStatusPublish(uint8_t mask) {
    bool hasPending = false;
    portENTER_CRITICAL(&_pendingWorkMux);
    if ((_pendingStatusPublishes & mask) != 0) {
        _pendingStatusPublishes &= (uint8_t)~mask;
        hasPending = true;
    }
    portEXIT_CRITICAL(&_pendingWorkMux);
    return hasPending;
}

static bool _takePendingNetworkRequest(uint8_t mask) {
    bool hasPending = false;
    portENTER_CRITICAL(&_pendingWorkMux);
    if ((_pendingNetworkRequests & mask) != 0) {
        _pendingNetworkRequests &= (uint8_t)~mask;
        hasPending = true;
    }
    portEXIT_CRITICAL(&_pendingWorkMux);
    return hasPending;
}

static bool _publishOnePendingStatus(void) {
    if (!_localMqtt.connected()) {
        return false;
    }

    if (_takePendingStatusPublish(LOCAL_STATUS_PUBLISH_SENSOR_CONFIG)) {
        _networkTaskCheckpoint("local_status_sensor_cfg");
        _publishSensorConfig();
        return true;
    }

    if (_takePendingStatusPublish(LOCAL_STATUS_PUBLISH_PH_CAL)) {
        _networkTaskCheckpoint("local_status_ph_cal");
        _publishPhCalibrationStatus();
        return true;
    }

    if (_takePendingStatusPublish(LOCAL_STATUS_PUBLISH_WATER)) {
        _networkTaskCheckpoint("local_status_water");
        _publishWaterSystemStatus();
        return true;
    }

    if (_takePendingStatusPublish(LOCAL_STATUS_PUBLISH_FAN)) {
        _networkTaskCheckpoint("local_status_fan");
        _publishFanStatus();
        return true;
    }

    if (_takePendingStatusPublish(LOCAL_STATUS_PUBLISH_LIGHT)) {
        _networkTaskCheckpoint("local_status_light");
        _publishLightStatus();
        return true;
    }

    if (_takePendingStatusPublish(LOCAL_STATUS_PUBLISH_FEEDER)) {
        _networkTaskCheckpoint("local_status_feeder");
        _publishFishFeederStatus();
        return true;
    }

    return false;
}

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
    gpioOutWrite(GPIO_OUT_PUMP_NUTRIENT_A, false);
    gpioOutWrite(GPIO_OUT_PUMP_NUTRIENT_B, false);

    _hwTestPumpActive = false;
    _hwTestPumpPin = -1;
    _hwTestPumpStopAt = 0;

    if (resumeAutomator) {
        automatorResume();
    }
}

static void _serviceScheduledStatusPublishes(void) {
    if (_takePendingNetworkRequest(LOCAL_NETWORK_REQUEST_NETPIE_SHADOW_SYNC)) {
        _networkTaskCheckpoint("local_netpie_shadow_sync");
        if (!netpieRequestShadowSync()) {
            _queueNetworkRequest(LOCAL_NETWORK_REQUEST_NETPIE_SHADOW_SYNC);
        }
        return;
    }

    if (_publishOnePendingStatus()) {
        return;
    }

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
    _networkTaskCheckpoint("local_mdns_query");
    IPAddress ip = MDNS.queryHost(LOCAL_MQTT_HOSTNAME, 200);
    _networkTaskCheckpoint("local_mdns_done");
    
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

static bool _enqueueDeferredAction(const LocalDeferredAction* action) {
    if (_deferredActionQueue == NULL || action == NULL) {
        return false;
    }

    if (xQueueSend(_deferredActionQueue, action, 0) == pdTRUE) {
        return true;
    }

    LOG_WARN("Deferred MQTT action queue full; dropping action type=%u", (unsigned int)action->type);
    return false;
}

static void _processDeferredAction(const LocalDeferredAction* action) {
    if (action == NULL) {
        return;
    }

    switch (action->type) {
        case LOCAL_DEFERRED_TDS_CAL:
        {
            TdsChannel channel = (action->data.tdsCal.channel == (uint8_t)TDS_CHANNEL_FISH)
                                    ? TDS_CHANNEL_FISH : TDS_CHANNEL_MIX;
            bool calOk = tdsSetCalibrationForChannel(
                channel,
                action->data.tdsCal.lowPpm,
                action->data.tdsCal.lowVoltage,
                action->data.tdsCal.lowTemperature,
                action->data.tdsCal.highPpm,
                action->data.tdsCal.highVoltage,
                action->data.tdsCal.highTemperature,
                action->data.tdsCal.rawVoltage
            );
            if (calOk) {
                LOG_INFO("TDS %s calibration received from Pi!",
                         channel == TDS_CHANNEL_FISH ? "fish" : "mix");
            } else {
                LOG_ERROR("TDS %s calibration rejected from Pi (check K / voltage span)",
                          channel == TDS_CHANNEL_FISH ? "fish" : "mix");
            }
            break;
        }

        case LOCAL_DEFERRED_PH_CAL:
        {
            PhChannel ch = (action->data.phCal.channel == (uint8_t)PH_CHANNEL_FISH)
                            ? PH_CHANNEL_FISH : PH_CHANNEL_MIX;
            const char* chName = (ch == PH_CHANNEL_FISH) ? "fish" : "mix";
            switch (action->data.phCal.action) {
                case LOCAL_PH_CAL_686:
                    if (phIsReadyChannel(ch)) {
                        phCalibratePh686Channel(ch);
                        LOG_INFO("pH 6.86 [%s] Calibration triggered from Pi Dashboard!", chName);
                        _queueStatusPublish(LOCAL_STATUS_PUBLISH_PH_CAL);
                    } else {
                        LOG_ERROR("pH %s sensor not ready for calibration", chName);
                    }
                    break;
                case LOCAL_PH_CAL_401:
                    if (phIsReadyChannel(ch)) {
                        phCalibratePh401Channel(ch);
                        LOG_INFO("pH 4.01 [%s] Calibration triggered from Pi Dashboard!", chName);
                        _queueStatusPublish(LOCAL_STATUS_PUBLISH_PH_CAL);
                    } else {
                        LOG_ERROR("pH %s sensor not ready for calibration", chName);
                    }
                    break;
                case LOCAL_PH_CAL_918:
                    if (phIsReadyChannel(ch)) {
                        phCalibratePh918Channel(ch);
                        LOG_INFO("pH 9.18 [%s] Calibration triggered from Pi Dashboard!", chName);
                        _queueStatusPublish(LOCAL_STATUS_PUBLISH_PH_CAL);
                    } else {
                        LOG_ERROR("pH %s sensor not ready for calibration", chName);
                    }
                    break;
                case LOCAL_PH_CAL_CLEAR:
                    phClearCalibrationChannel(ch);
                    LOG_INFO("pH [%s] Calibration cleared from Pi Dashboard!", chName);
                    _queueStatusPublish(LOCAL_STATUS_PUBLISH_PH_CAL);
                    break;
                default:
                    break;
            }
            break;
        }

        case LOCAL_DEFERRED_SENSOR_CONFIG:
        {
            bool states[SENSOR_COUNT];
            for (int i = 0; i < SENSOR_COUNT; i++) {
                states[i] = action->data.sensors.states[i];
            }
            systemSetAllSensorsEnabled(states);
            LOG_INFO("Sensor config updated via MQTT (batch)");
            _queueStatusPublish(LOCAL_STATUS_PUBLISH_SENSOR_CONFIG);
            break;
        }

        case LOCAL_DEFERRED_AUTOMATION_CONFIG:
            automatorSetConfig(
                action->data.automation.enabled,
                action->data.automation.targetTds,
                action->data.automation.doseAVolumeMl,
                action->data.automation.doseBVolumeMl,
                action->data.automation.mixAfterAMs,
                action->data.automation.postDoseMixMs,
                action->data.automation.tdsHysteresisPpm
            );
            LOG_INFO(
                "Automation Config updated: En=%d, TDS=%.1f, doseA=%.2f, doseB=%.2f, mixA=%lu, postMix=%lu, hyst=%.1f",
                action->data.automation.enabled,
                action->data.automation.targetTds,
                action->data.automation.doseAVolumeMl,
                action->data.automation.doseBVolumeMl,
                action->data.automation.mixAfterAMs,
                action->data.automation.postDoseMixMs,
                action->data.automation.tdsHysteresisPpm
            );
            break;

        case LOCAL_DEFERRED_LIGHT_CONFIG:
            lightCtrlSetConfig(
                action->data.light.source,
                action->data.light.enabled,
                action->data.light.manualState,
                action->data.light.onDay,
                action->data.light.onTime,
                action->data.light.offDay,
                action->data.light.offTime
            );
            LOG_INFO(
                "Light config updated from Local Web: Enabled=%d, Manual=%d, On=%d %s, Off=%d %s",
                action->data.light.enabled,
                action->data.light.manualState,
                action->data.light.onDay,
                action->data.light.onTime,
                action->data.light.offDay,
                action->data.light.offTime
            );
            _queueStatusPublish(LOCAL_STATUS_PUBLISH_LIGHT);
            if (action->data.light.requestNetpieShadowSync) {
                _queueNetworkRequest(LOCAL_NETWORK_REQUEST_NETPIE_SHADOW_SYNC);
            }
            break;

        case LOCAL_DEFERRED_FEEDER_CONFIG:
            fishFeederSetConfig(
                action->data.feeder.source,
                action->data.feeder.enabled,
                action->data.feeder.feedDay,
                action->data.feeder.feedTime,
                action->data.feeder.durationMs
            );
            if (action->data.feeder.triggerFeed) {
                fishFeederStartManualFeed("Manual feed triggered from Local Web");
            }
            LOG_INFO(
                "Fish feeder config updated from Local Web: Enabled=%d, Day=%d, Time=%s, Duration=%lu",
                action->data.feeder.enabled,
                action->data.feeder.feedDay,
                action->data.feeder.feedTime,
                action->data.feeder.durationMs
            );
            _queueStatusPublish(LOCAL_STATUS_PUBLISH_FEEDER);
            break;

        case LOCAL_DEFERRED_FAN_CONFIG:
            fanCtrlSetConfig(
                action->data.fan.enabled,
                action->data.fan.autoMode,
                action->data.fan.manualState,
                action->data.fan.tempOnC,
                action->data.fan.tempOffC,
                action->data.fan.humidityOnPct,
                action->data.fan.humidityOffPct
            );
            LOG_INFO(
                "Fan Config updated: En=%d, Auto=%d, Manual=%d, T=%.1f/%.1f, H=%.1f/%.1f",
                action->data.fan.enabled,
                action->data.fan.autoMode,
                action->data.fan.manualState,
                action->data.fan.tempOnC,
                action->data.fan.tempOffC,
                action->data.fan.humidityOnPct,
                action->data.fan.humidityOffPct
            );
            _queueStatusPublish(LOCAL_STATUS_PUBLISH_FAN);
            break;

        case LOCAL_DEFERRED_WATER_CONFIG:
            waterSystemSetConfig(
                action->data.water.circulationEnabled,
                action->data.water.refillEnabled,
                action->data.water.refillMaxRuntimeMs,
                action->data.water.refillMinIntervalMs,
                action->data.water.preferredRoute,
                action->data.water.allowDirectSumpRefill,
                action->data.water.fishRefillIntervalMs,
                action->data.water.fishRefillMaxRuntimeMs
            );
            if (action->data.water.applyManualRefill) {
                waterSystemSetManualRefill(action->data.water.manualRefill);
            }
            if (action->data.water.clearAlarm) {
                waterSystemClearAlarm();
            }
            LOG_INFO(
                "Water System config updated: Circ=%d, Refill=%d, Route=%s, Direct=%d, Max=%lu ms, MinInt=%lu ms, FishInt=%lu ms, FishMax=%lu ms",
                action->data.water.circulationEnabled,
                action->data.water.refillEnabled,
                waterSystemGetRouteString(action->data.water.preferredRoute),
                action->data.water.allowDirectSumpRefill,
                action->data.water.refillMaxRuntimeMs,
                action->data.water.refillMinIntervalMs,
                action->data.water.fishRefillIntervalMs,
                action->data.water.fishRefillMaxRuntimeMs
            );
            _queueStatusPublish(LOCAL_STATUS_PUBLISH_WATER);
            break;

        default:
            break;
    }
}

/**
 * @brief MQTT Callback for receiving commands from Pi
 */
static void _onMqttMessage(char* topic, byte* payload, unsigned int length) {
    // Parse JSON payload
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    
    if (err) {
        LOG_ERROR("Local MQTT JSON parse error: %s", err.c_str());
        return;
    }
    
    // Handle TDS Calibration
    if (strcmp(topic, "aquaponics/config/tds_cal") == 0) {
        const char* channelStr = doc["channel"] | "mix";
        LocalDeferredAction action = {};
        action.type = LOCAL_DEFERRED_TDS_CAL;
        action.data.tdsCal.channel = (strcmp(channelStr, "fish") == 0)
                                        ? (uint8_t)TDS_CHANNEL_FISH
                                        : (uint8_t)TDS_CHANNEL_MIX;
        action.data.tdsCal.lowPpm = doc["low_ppm"] | 0.0f;
        action.data.tdsCal.lowVoltage = doc["low_voltage"] | 0.0f;
        action.data.tdsCal.lowTemperature = doc["low_temp"] | 25.0f;
        action.data.tdsCal.highPpm = doc["high_ppm"] | 0.0f;
        action.data.tdsCal.highVoltage = doc["high_voltage"] | 0.0f;
        action.data.tdsCal.highTemperature = doc["high_temp"] | 25.0f;
        action.data.tdsCal.rawVoltage = doc["raw_voltage"] | false;

        if (action.data.tdsCal.lowVoltage > 0 && action.data.tdsCal.highVoltage > 0 &&
            action.data.tdsCal.lowVoltage != action.data.tdsCal.highVoltage) {
            _enqueueDeferredAction(&action);
        } else {
            LOG_ERROR("Invalid TDS calibration data received");
        }
    }
    
    // Handle pH Calibration
    if (strcmp(topic, "aquaponics/config/ph_cal") == 0) {
        const char* action = doc["action"] | "";
        const char* channelStr = doc["channel"] | "mix";
        uint8_t channel = (strcmp(channelStr, "fish") == 0) ? (uint8_t)PH_CHANNEL_FISH
                                                            : (uint8_t)PH_CHANNEL_MIX;
        
        if (strcmp(action, "cal686") == 0 || strcmp(action, "cal7") == 0) {
            LocalDeferredAction deferred = {};
            deferred.type = LOCAL_DEFERRED_PH_CAL;
            deferred.data.phCal.action = LOCAL_PH_CAL_686;
            deferred.data.phCal.channel = channel;
            _enqueueDeferredAction(&deferred);
        } else if (strcmp(action, "cal401") == 0 || strcmp(action, "cal4") == 0) {
            LocalDeferredAction deferred = {};
            deferred.type = LOCAL_DEFERRED_PH_CAL;
            deferred.data.phCal.action = LOCAL_PH_CAL_401;
            deferred.data.phCal.channel = channel;
            _enqueueDeferredAction(&deferred);
        } else if (strcmp(action, "cal918") == 0) {
            LocalDeferredAction deferred = {};
            deferred.type = LOCAL_DEFERRED_PH_CAL;
            deferred.data.phCal.action = LOCAL_PH_CAL_918;
            deferred.data.phCal.channel = channel;
            _enqueueDeferredAction(&deferred);
        } else if (strcmp(action, "clear") == 0) {
            LocalDeferredAction deferred = {};
            deferred.type = LOCAL_DEFERRED_PH_CAL;
            deferred.data.phCal.action = LOCAL_PH_CAL_CLEAR;
            deferred.data.phCal.channel = channel;
            _enqueueDeferredAction(&deferred);
        }
    }
    
    // Handle Sensor Toggle Configuration (batch update to prevent NVS race condition)
    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_SENSORS) == 0) {
        LocalDeferredAction action = {};
        action.type = LOCAL_DEFERRED_SENSOR_CONFIG;
        // Read current states as defaults
        for (int i = 0; i < SENSOR_COUNT; i++) {
            action.data.sensors.states[i] = systemGetSensorEnabled((SensorId_t)i);
        }
        
        // Override with values from MQTT message
        if (doc.containsKey("tds")) action.data.sensors.states[SENSOR_TDS] = doc["tds"];
        if (doc.containsKey("ph")) action.data.sensors.states[SENSOR_PH] = doc["ph"];
        if (doc.containsKey("water")) action.data.sensors.states[SENSOR_WATER_TEMP] = doc["water"];
        if (doc.containsKey("air")) action.data.sensors.states[SENSOR_AIR_TEMP] = doc["air"];
        if (doc.containsKey("light")) action.data.sensors.states[SENSOR_LIGHT] = doc["light"];
        _enqueueDeferredAction(&action);
    }
    
    // Handle Automation Target Config
    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_AUTOMATION) == 0) {
        AutomatorConfig cfg;
        automatorGetConfig(&cfg);
        LocalDeferredAction action = {};
        action.type = LOCAL_DEFERRED_AUTOMATION_CONFIG;
        action.data.automation.enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : cfg.enabled;
        action.data.automation.targetTds = doc.containsKey("target_tds") ? doc["target_tds"].as<float>() : cfg.targetTds;
        action.data.automation.doseAVolumeMl = doc.containsKey("dose_a_ml") ? doc["dose_a_ml"].as<float>() : cfg.doseAVolumeMl;
        action.data.automation.doseBVolumeMl = doc.containsKey("dose_b_ml") ? doc["dose_b_ml"].as<float>() : cfg.doseBVolumeMl;
        action.data.automation.mixAfterAMs = doc.containsKey("mix_after_a_ms") ? doc["mix_after_a_ms"].as<unsigned long>() : cfg.mixAfterAMs;
        action.data.automation.postDoseMixMs = doc.containsKey("post_dose_mix_ms") ? doc["post_dose_mix_ms"].as<unsigned long>() : cfg.postDoseMixMs;
        action.data.automation.tdsHysteresisPpm = doc.containsKey("tds_hysteresis_ppm") ? doc["tds_hysteresis_ppm"].as<float>() : cfg.tdsHysteresisPpm;
        _enqueueDeferredAction(&action);
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_LIGHT_CONTROL) == 0) {
        LightControlConfig cfg;
        lightCtrlGetConfig(&cfg);
        CommandSource targetSource = cfg.commandSource;
        bool switchedToNetpie = false;

        if (doc.containsKey("command_source")) {
            targetSource = _parseCommandSource(doc["command_source"] | "", cfg.commandSource);
            switchedToNetpie = (cfg.commandSource != targetSource && targetSource == COMMAND_SOURCE_NETPIE);
        }

        if (cfg.commandSource == COMMAND_SOURCE_LOCAL_WEB || doc.containsKey("command_source")) {
            LocalDeferredAction action = {};
            action.type = LOCAL_DEFERRED_LIGHT_CONFIG;
            action.data.light.source = targetSource;
            bool allowFieldUpdates = (targetSource == COMMAND_SOURCE_LOCAL_WEB);
            action.data.light.enabled = allowFieldUpdates && doc.containsKey("enabled") ? doc["enabled"].as<bool>() : cfg.enabled;
            action.data.light.manualState = allowFieldUpdates && doc.containsKey("manual_state") ? doc["manual_state"].as<bool>() : cfg.manualState;
            action.data.light.onDay = allowFieldUpdates && doc.containsKey("on_day") ? doc["on_day"].as<int>() : cfg.onDay;
            action.data.light.offDay = allowFieldUpdates && doc.containsKey("off_day") ? doc["off_day"].as<int>() : cfg.offDay;
            snprintf(action.data.light.onTime, sizeof(action.data.light.onTime), "%s", allowFieldUpdates && doc.containsKey("on_time") ? (doc["on_time"] | cfg.onTime) : cfg.onTime);
            snprintf(action.data.light.offTime, sizeof(action.data.light.offTime), "%s", allowFieldUpdates && doc.containsKey("off_time") ? (doc["off_time"] | cfg.offTime) : cfg.offTime);
            action.data.light.requestNetpieShadowSync = switchedToNetpie;
            _enqueueDeferredAction(&action);
        } else {
            LOG_WARN("Local Web light config ignored because control source is not local_web");
        }
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_FISH_FEEDER) == 0) {
        FishFeederConfig cfg;
        fishFeederGetConfig(&cfg);
        CommandSource targetSource = cfg.commandSource;

        if (doc.containsKey("command_source")) {
            targetSource = _parseCommandSource(doc["command_source"] | "", cfg.commandSource);
        }

        if (cfg.commandSource == COMMAND_SOURCE_LOCAL_WEB || doc.containsKey("command_source")) {
            LocalDeferredAction action = {};
            action.type = LOCAL_DEFERRED_FEEDER_CONFIG;
            action.data.feeder.source = targetSource;
            bool allowFieldUpdates = (targetSource == COMMAND_SOURCE_LOCAL_WEB);
            action.data.feeder.enabled = allowFieldUpdates && doc.containsKey("enabled") ? doc["enabled"].as<bool>() : cfg.enabled;
            action.data.feeder.feedDay = allowFieldUpdates && doc.containsKey("feed_day") ? doc["feed_day"].as<int>() : cfg.feedDay;
            snprintf(action.data.feeder.feedTime, sizeof(action.data.feeder.feedTime), "%s", allowFieldUpdates && doc.containsKey("feed_time") ? (doc["feed_time"] | cfg.feedTime) : cfg.feedTime);
            action.data.feeder.durationMs = allowFieldUpdates && doc.containsKey("duration_ms") ? doc["duration_ms"].as<unsigned long>() : cfg.durationMs;
            action.data.feeder.triggerFeed = allowFieldUpdates && (doc["trigger_feed"] | false);
            _enqueueDeferredAction(&action);
        } else {
            LOG_WARN("Local Web fish feeder config ignored because control source is not local_web");
        }
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_FAN_CONTROL) == 0) {
        FanControlConfig cfg;
        fanCtrlGetConfig(&cfg);

        LocalDeferredAction action = {};
        action.type = LOCAL_DEFERRED_FAN_CONFIG;
        action.data.fan.enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : cfg.enabled;
        action.data.fan.autoMode = doc.containsKey("auto_mode") ? doc["auto_mode"].as<bool>() : cfg.autoMode;
        action.data.fan.manualState = doc.containsKey("manual_state") ? doc["manual_state"].as<bool>() : cfg.manualState;
        action.data.fan.tempOnC = doc.containsKey("temp_on_c") ? doc["temp_on_c"].as<float>() : cfg.tempOnC;
        action.data.fan.tempOffC = doc.containsKey("temp_off_c") ? doc["temp_off_c"].as<float>() : cfg.tempOffC;
        action.data.fan.humidityOnPct = doc.containsKey("humidity_on_pct") ? doc["humidity_on_pct"].as<float>() : cfg.humidityOnPct;
        action.data.fan.humidityOffPct = doc.containsKey("humidity_off_pct") ? doc["humidity_off_pct"].as<float>() : cfg.humidityOffPct;
        _enqueueDeferredAction(&action);
    }

    if (strcmp(topic, LOCAL_MQTT_TOPIC_CONFIG_WATER_SYSTEM) == 0) {
        WaterSystemConfig cfg;
        waterSystemGetConfig(&cfg);

        LocalDeferredAction action = {};
        action.type = LOCAL_DEFERRED_WATER_CONFIG;
        action.data.water.circulationEnabled = doc.containsKey("circulation_enabled") ? doc["circulation_enabled"].as<bool>() : cfg.circulationEnabled;
        action.data.water.refillEnabled = doc.containsKey("refill_enabled") ? doc["refill_enabled"].as<bool>() : cfg.refillEnabled;
        action.data.water.refillMaxRuntimeMs = doc.containsKey("refill_max_runtime_ms") ? doc["refill_max_runtime_ms"].as<unsigned long>() : cfg.refillMaxRuntimeMs;
        action.data.water.refillMinIntervalMs = doc.containsKey("refill_min_interval_ms") ? doc["refill_min_interval_ms"].as<unsigned long>() : cfg.refillMinIntervalMs;
        action.data.water.preferredRoute = cfg.preferredRoute;
        action.data.water.allowDirectSumpRefill = doc.containsKey("allow_direct_sump_refill") ? doc["allow_direct_sump_refill"].as<bool>() : cfg.allowDirectSumpRefill;
        action.data.water.fishRefillIntervalMs = doc.containsKey("fish_refill_interval_ms") ? doc["fish_refill_interval_ms"].as<unsigned long>() : cfg.fishRefillIntervalMs;
        action.data.water.fishRefillMaxRuntimeMs = doc.containsKey("fish_refill_max_runtime_ms") ? doc["fish_refill_max_runtime_ms"].as<unsigned long>() : cfg.fishRefillMaxRuntimeMs;
        action.data.water.applyManualRefill = doc.containsKey("manual_refill");
        action.data.water.manualRefill = action.data.water.applyManualRefill ? doc["manual_refill"].as<bool>() : false;
        action.data.water.clearAlarm = doc["clear_alarm"] | false;

        if (doc.containsKey("preferred_route")) {
            action.data.water.preferredRoute = _parseWaterRoute(doc["preferred_route"] | "", cfg.preferredRoute);
        }

        _enqueueDeferredAction(&action);
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
            gpioOutWrite(GPIO_OUT_PUMP_NUTRIENT_A, true);
            result["status"] = "running";
            result["duration_ms"] = duration;
            result["gpio"] = PUMP_NUTRIENT_A_PIN;
            LOG_INFO("[HW TEST] Pump A ON for %d ms", duration);
        }
        else if (strcmp(cmd, "pump_b") == 0) {
            _stopHwTestPumpOutputs(false);
            gpioOutWrite(GPIO_OUT_PUMP_NUTRIENT_B, true);
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
            float td = tdsGetLastValue();
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

    _networkTaskCheckpoint("local_mqtt_connect");
    if (_localMqtt.connect(clientId, NULL, NULL, "aquaponics/status/online", 1, true, "offline")) {
        _networkTaskCheckpoint("local_mqtt_connected");
        LOG_INFO("✅ Connected to Local MQTT!");
        _connectionFailCount = 0; // Reset counter on success
        
        // Publish online status (retained)
        _localMqtt.publish("aquaponics/status/online", "online", true);
        
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

        _queueReconnectStatusPublishes();
        
        return true;
    } else {
        _networkTaskCheckpoint("local_mqtt_connect_fail");
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
    _deferredActionQueue = xQueueCreate(LOCAL_MQTT_DEFERRED_QUEUE_LENGTH, sizeof(LocalDeferredAction));
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
        _networkTaskCheckpoint("local_mqtt_poll");
        _localMqtt.loop();
        _networkTaskCheckpoint("local_mqtt_post_poll");
        _serviceScheduledStatusPublishes();
        
        // Process cross-core log queue safely in the Networking Task
        if (_logQueue != NULL) {
            char logBuff[128];
            int count = 0;
            // Process a small number of logs per loop to avoid long publish bursts.
            while (count < LOCAL_MQTT_MAX_LOGS_PER_LOOP && xQueueReceive(_logQueue, logBuff, 0) == pdTRUE) {
                _networkTaskCheckpoint("local_mqtt_log_publish");
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
        _networkTaskCheckpoint("local_hwtest_done");
        _localMqtt.publish(LOCAL_MQTT_TOPIC_HW_TEST_RESULT, buf);
        LOG_INFO("[HW TEST] Published 'completed' for %s", _hwTestCompletedCmd);
    }
}

void localMqttProcessDeferredActions(void) {
    if (_deferredActionQueue == NULL) {
        return;
    }

    LocalDeferredAction action;
    uint8_t processed = 0;
    while (processed < LOCAL_MQTT_MAX_DEFERRED_ACTIONS_PER_LOOP &&
           xQueueReceive(_deferredActionQueue, &action, 0) == pdTRUE) {
        _processDeferredAction(&action);
        processed++;
    }
}

bool localMqttIsConnected(void) {
    return _localMqtt.connected();
}

void localMqttPublishData(float waterTemp,
                          float waterTempFish,
                          float airTemp,
                          float humidity,
                          float tds,
                          float tdsFish,
                          float light,
                          float ph) {
    if (millis() - _lastPublishTime < LOCAL_PUBLISH_INTERVAL) {
        return;
    }
    
    if (!localMqttIsConnected()) return;

    // Flush keepalive / detect stale connection before expensive publish
    _networkTaskCheckpoint("local_sensor_preloop");
    _localMqtt.loop();
    if (!_localMqtt.connected()) return;
    
    _lastPublishTime = millis();

    _sensorPublishDoc.clear();
    
    // Format same as Netpie for consistency, or simpler flat JSON
    if (!isnan(waterTemp)) {
        _sensorPublishDoc["water_temp"] = round(waterTemp * 10) / 10.0;
        _sensorPublishDoc["water_temp_mix"] = round(waterTemp * 10) / 10.0;
    }
    if (!isnan(waterTempFish)) _sensorPublishDoc["water_temp_fish"] = round(waterTempFish * 10) / 10.0;
    if (!isnan(airTemp)) _sensorPublishDoc["air_temp"] = round(airTemp * 10) / 10.0;
    if (!isnan(humidity)) _sensorPublishDoc["humidity"] = round(humidity * 10) / 10.0;
    if (tds >= 0) {
        _sensorPublishDoc["tds"] = round(tds);
        _sensorPublishDoc["tds_mix"] = round(tds);
    }
    if (tdsFish >= 0) _sensorPublishDoc["tds_fish"] = round(tdsFish);
    _sensorPublishDoc["tds_mix_cal_k"] = round(tdsGetCalibrationKForChannel(TDS_CHANNEL_MIX) * 1000) / 1000.0;
    _sensorPublishDoc["tds_mix_cal_quality_ok"] = tdsIsCalibrationQualityOkForChannel(TDS_CHANNEL_MIX);
    _sensorPublishDoc["tds_mix_below_cal_range"] = tdsIsVoltageBelowCalibrationRangeForChannel(TDS_CHANNEL_MIX);
    if (light >= 0) _sensorPublishDoc["light"] = round(light * 10) / 10.0;
    if (ph >= 0) {
        _sensorPublishDoc["ph"] = round(ph * 10) / 10.0;
        _sensorPublishDoc["ph_mix"] = round(ph * 10) / 10.0;
    }
    float phFish = phReadChannel(PH_CHANNEL_FISH);
    if (phFish >= 0 && !isnan(phFish)) {
        _sensorPublishDoc["ph_fish"] = round(phFish * 10) / 10.0;
    }
    
    // Add TDS voltage for calibration
    float tdsVoltage = tdsGetVoltage();
    if (tdsVoltage >= 0) _sensorPublishDoc["tds_voltage"] = round(tdsVoltage * 1000) / 1000.0;
    float tdsFishVoltage = tdsGetVoltageForChannel(TDS_CHANNEL_FISH);
    if (tdsFishVoltage >= 0) _sensorPublishDoc["tds_fish_voltage"] = round(tdsFishVoltage * 1000) / 1000.0;
    
    // Add pH voltage for calibration (mV)
    float phVoltage = phReadVoltage();
    if (phVoltage >= 0) {
        _sensorPublishDoc["ph_voltage"] = round(phVoltage * 10) / 10.0;
        _sensorPublishDoc["ph_mix_voltage"] = round(phVoltage * 10) / 10.0;
    }
    float phFishVoltage = phReadVoltageChannel(PH_CHANNEL_FISH);
    if (phFishVoltage >= 0) {
        _sensorPublishDoc["ph_fish_voltage"] = round(phFishVoltage * 10) / 10.0;
    }
    if (phIsReady()) _sensorPublishDoc["ph_value"] = round(phRead() * 10) / 10.0;
    
    // Add Network Connectivity Status
    _sensorPublishDoc["mqtt_connected"] = netpieIsConnected(); // Status for Dashboard
    _sensorPublishDoc["wifi_rssi"] = WiFi.RSSI();
    
    // Add System Health Stats
    SystemHealth_t health;
    systemGetHealth(&health);
    _sensorPublishDoc["uptime_sec"] = health.uptimeMs / 1000;
    _sensorPublishDoc["free_heap"] = health.freeHeap;
    _sensorPublishDoc["heap_size"] = health.heapSize; // Added for RAM calc
    _sensorPublishDoc["wifi_reconnects"] = health.wifiReconnects;
    _sensorPublishDoc["mqtt_reconnects"] = health.mqttReconnects;
    _sensorPublishDoc["watchdog_resets"] = health.watchdogResets;
    _sensorPublishDoc["reset_reason"] = health.resetReason;
    _sensorPublishDoc["cpu_temp"] = health.cpuTemp; // ESP32 Temp
    
    // Add Automator Process State (Process Tracker)
    AutomatorConfig authCfg;
    automatorGetConfig(&authCfg);
    _sensorPublishDoc["auto_enabled"] = authCfg.enabled;
    _sensorPublishDoc["auto_tgt_tds"] = authCfg.targetTds;
    _sensorPublishDoc["auto_dose_a_ml"] = authCfg.doseAVolumeMl;
    _sensorPublishDoc["auto_dose_b_ml"] = authCfg.doseBVolumeMl;
    _sensorPublishDoc["auto_mix_after_a_ms"] = authCfg.mixAfterAMs;
    _sensorPublishDoc["auto_post_dose_mix_ms"] = authCfg.postDoseMixMs;
    _sensorPublishDoc["auto_tds_hysteresis_ppm"] = authCfg.tdsHysteresisPpm;
    _sensorPublishDoc["auto_state"] = automatorGetStateString(automatorGetCurrentState());
    _sensorPublishDoc["auto_next_state"] = automatorGetNextStateString();
    _sensorPublishDoc["auto_reason"] = automatorGetActionReason();
    _sensorPublishDoc["auto_time_left"] = automatorGetTimeRemainingSec();

    LightControlConfig lightCfg;
    LightControlStatus lightStatus;
    lightCtrlGetConfig(&lightCfg);
    lightCtrlGetStatus(&lightStatus);
    _sensorPublishDoc["light_relay"] = lightStatus.running;
    _sensorPublishDoc["light_schedule_enabled"] = lightCfg.enabled;
    _sensorPublishDoc["light_manual_state"] = lightCfg.manualState;
    _sensorPublishDoc["light_source"] = lightCtrlGetCommandSourceString(lightCfg.commandSource);

    FanControlConfig fanCfg;
    FanControlStatus fanStatus;
    fanCtrlGetConfig(&fanCfg);
    fanCtrlGetStatus(&fanStatus);
    _sensorPublishDoc["fan_enabled"] = fanCfg.enabled;
    _sensorPublishDoc["fan_auto_mode"] = fanCfg.autoMode;
    _sensorPublishDoc["fan_manual_state"] = fanCfg.manualState;
    _sensorPublishDoc["fan_running"] = fanStatus.running;
    _sensorPublishDoc["fan_state"] = fanCtrlGetStateString(fanStatus.state);
    _sensorPublishDoc["fan_reason"] = fanStatus.reason;
    _sensorPublishDoc["fan_has_output"] = fanStatus.hasOutput;

    FishFeederConfig feederCfg;
    FishFeederStatus feederStatus;
    fishFeederGetConfig(&feederCfg);
    fishFeederGetStatus(&feederStatus);
    _sensorPublishDoc["feeder_enabled"] = feederCfg.enabled;
    _sensorPublishDoc["feeder_running"] = feederStatus.running;
    _sensorPublishDoc["feeder_state"] = fishFeederGetStateString(feederStatus.state);
    _sensorPublishDoc["feeder_source"] = commandSourceToString(feederCfg.commandSource);

    WaterSystemStatus waterStatus;
    waterSystemGetStatus(&waterStatus);
    _sensorPublishDoc["water_status_seen"] = true;
    _sensorPublishDoc["water_state"] = waterSystemGetStateString(waterStatus.state);
    _sensorPublishDoc["active_route"] = waterSystemGetRouteString(waterStatus.activeRoute);
    _sensorPublishDoc["circulation_output"] = waterStatus.circulationOutput;
    _sensorPublishDoc["refill_output"] = waterStatus.refillOutput;
    _sensorPublishDoc["circulation_pump_output"] = waterStatus.circulationPumpOutput;
    _sensorPublishDoc["fish_tank_refill_output"] = waterStatus.fishTankRefillOutput;
    _sensorPublishDoc["mix_tank_refill_output"] = waterStatus.mixTankRefillOutput;
    _sensorPublishDoc["route_blocked"] = waterStatus.routeBlocked;
    _sensorPublishDoc["water_alarm"] = waterStatus.alarmActive;
    _sensorPublishDoc["sump_low"] = waterStatus.levelLow;
    _sensorPublishDoc["sump_high"] = waterStatus.levelHigh;
    _sensorPublishDoc["fish_overflow"] = waterStatus.overflowAlarm;
    _sensorPublishDoc["has_level_sensors"] = waterStatus.hasLevelSensors;
    _sensorPublishDoc["has_overflow_sensor"] = waterStatus.hasOverflowSensor;

    if (_sensorPublishDoc.overflowed()) {
        LOG_ERROR("Local MQTT sensor JSON overflowed before serialize; skipping publish");
        return;
    }

    size_t payloadLen = serializeJson(_sensorPublishDoc, _sensorPublishPayload, sizeof(_sensorPublishPayload));

    if (payloadLen == 0 || payloadLen >= sizeof(_sensorPublishPayload) - 1) {
        LOG_ERROR("Local MQTT payload too large (%u bytes), skipping publish", (unsigned int)payloadLen);
        return;
    }

    _networkTaskCheckpoint("local_sensor_publish");
    if (_localMqtt.publish(LOCAL_MQTT_TOPIC_SENSORS, _sensorPublishPayload)) {
        _networkTaskCheckpoint("local_sensor_publish_ok");
        LOG_DEBUG("Local MQTT Publish: %s", _sensorPublishPayload);

        // Keep the dedicated water status topic warm whenever sensor packets are flowing.
        // This closes the gap where aquaponics/sensors stays fresh but aquaponics/status/water_system lags.
        unsigned long now = millis();
        if (now - _lastWaterStatusPublishTime >= LOCAL_PUBLISH_INTERVAL) {
            _lastWaterStatusPublishTime = now;
            _queueStatusPublish(LOCAL_STATUS_PUBLISH_WATER);
        }
    } else {
        _networkTaskCheckpoint("local_sensor_publish_fail");
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

    StaticJsonDocument<512> doc;
    // Legacy keys = mix channel (kept for Pi UI backward compat)
    doc["ph_voltage"] = round(phReadVoltage() * 10) / 10.0;
    doc["ph_value"] = round(phRead() * 100) / 100.0;
    doc["calibrated"] = phHasCalibration401() || phHasCalibration686() || phHasCalibration918();
    doc["cal401_done"] = phHasCalibration401();
    doc["cal686_done"] = phHasCalibration686();
    doc["cal918_done"] = phHasCalibration918();

    // Multi-channel keys (mix + fish)
    JsonObject mix = doc.createNestedObject("mix");
    mix["ph_voltage"] = round(phReadVoltageChannel(PH_CHANNEL_MIX) * 10) / 10.0;
    mix["ph_value"] = round(phReadChannel(PH_CHANNEL_MIX) * 100) / 100.0;
    mix["cal401_done"] = phHasCalibration401Channel(PH_CHANNEL_MIX);
    mix["cal686_done"] = phHasCalibration686Channel(PH_CHANNEL_MIX);
    mix["cal918_done"] = phHasCalibration918Channel(PH_CHANNEL_MIX);

    JsonObject fish = doc.createNestedObject("fish");
    fish["ph_voltage"] = round(phReadVoltageChannel(PH_CHANNEL_FISH) * 10) / 10.0;
    fish["ph_value"] = round(phReadChannel(PH_CHANNEL_FISH) * 100) / 100.0;
    fish["cal401_done"] = phHasCalibration401Channel(PH_CHANNEL_FISH);
    fish["cal686_done"] = phHasCalibration686Channel(PH_CHANNEL_FISH);
    fish["cal918_done"] = phHasCalibration918Channel(PH_CHANNEL_FISH);

    char buffer[512];
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

    StaticJsonDocument<1536> doc;
    doc["circulation_enabled"] = cfg.circulationEnabled;
    doc["refill_enabled"] = cfg.refillEnabled;
    doc["manual_refill"] = cfg.manualRefill;
    doc["refill_max_runtime_ms"] = cfg.refillMaxRuntimeMs;
    doc["refill_min_interval_ms"] = cfg.refillMinIntervalMs;
    doc["preferred_route"] = waterSystemGetRouteString(cfg.preferredRoute);
    doc["allow_direct_sump_refill"] = cfg.allowDirectSumpRefill;
    doc["fish_refill_interval_ms"] = cfg.fishRefillIntervalMs;
    doc["fish_refill_max_runtime_ms"] = cfg.fishRefillMaxRuntimeMs;
    doc["state"] = waterSystemGetStateString(status.state);
    doc["state_label_th"] = waterSystemGetStateLabelTh(status.state);
    doc["reason"] = status.reason;
    doc["circulation_output"] = status.circulationOutput;
    doc["refill_output"] = status.refillOutput;
    doc["circulation_pump_output"] = status.circulationPumpOutput;
    doc["fish_tank_refill_output"] = status.fishTankRefillOutput;
    doc["mix_tank_refill_output"] = status.mixTankRefillOutput;
    doc["water_dilution_active"] = status.waterDilutionActive;
    doc["mix_tank_settling_active"] = status.mixTankSettlingActive;
    doc["mix_tank_control_zone"] = status.mixTankControlZone;
    doc["dilution_hold_remaining_ms"] = status.dilutionHoldRemainingMs;
    doc["fish_refill_ready"] = status.fishRefillReady;
    doc["fish_refill_wait_remaining_ms"] = status.fishRefillWaitRemainingMs;
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

    const size_t payloadLen = measureJson(doc);
    if (doc.overflowed() || payloadLen >= 1536) {
        LOG_ERROR("Local MQTT water status JSON overflowed; skipping publish");
        return;
    }

    char buffer[1536];
    size_t serializedLen = serializeJson(doc, buffer, sizeof(buffer));

    _networkTaskCheckpoint("local_status_water_publish");
    if (!_localMqtt.publish(LOCAL_MQTT_TOPIC_STATUS_WATER_SYSTEM, buffer)) {
        LOG_ERROR("Local MQTT water status publish failed (len=%u, state=%d)",
                  (unsigned int)serializedLen,
                  _localMqtt.state());
    }
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


