/**
 * @file main.cpp
 * @brief โปรแกรมหลักสำหรับระบบ Aquaponics Sensor
 * @details รวมเซ็นเซอร์ TDS, DHT22, DS18B20, BH1750 พร้อม WiFi, NETPIE, Light Control และ Telnet Logging
 */

#include <Arduino.h>
#include "config.h"
#include "logger.h"
#include "system.h"
#include "ota.h"
#include "telnetServer.h"
#include "TdsSensor.h"
#include "dhtSensor.h"
#include "tempSensor.h"
#include "lightSensor.h"
#include "phSensor.h"
#include "lightController.h"
#include "fishFeeder.h"
#include "fanController.h"
#include "wifiConn.h"
#include "netpie.h"
#include "localMqtt.h"
#include "commandHandler.h"
#include "automator.h"
#include "waterSystem.h"


#if defined(ESP32) && WATCHDOG_ENABLED
#include "esp_task_wdt.h"
#endif

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

static float currentWaterTemp = NAN;  // อุณหภูมิน้ำปัจจุบัน (°C)
static float currentAirTemp = NAN;    // อุณหภูมิอากาศ (°C)
static float currentHumidity = NAN;   // ความชื้น (%)
static float currentTds = -1;         // ค่า TDS (ppm)
static float currentLight = -1;       // ความเข้มแสง (lux)
static float currentPh = -1;          // ค่า pH

static void taskCheckpoint(TaskId_t taskId, const char* stage) {
    systemSetTaskProgress(taskId, stage);
    systemTaskHeartbeat(taskId);

    #if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_reset();
    #endif
}

// ============================================================================
// VALIDATION FUNCTIONS
// ============================================================================

static float validateTds(float tds) {
    if (tds < 0 || isnan(tds)) return -1.0f;
    if (tds < TDS_MIN) return TDS_MIN;
    if (tds > TDS_MAX) return TDS_MAX;
    return tds;
}

static float validatePh(float ph) {
    if (ph < 0 || isnan(ph)) return -1.0f;
    if (ph < PH_MIN) return PH_MIN;
    if (ph > PH_MAX) return PH_MAX;
    return ph;
}

static float validateTemperature(float temp) {
    if (isnan(temp)) return NAN;
    if (temp < TEMP_MIN || temp > TEMP_MAX) return NAN;
    return temp;
}

static float validateHumidity(float humidity) {
    if (isnan(humidity)) return NAN;
    if (humidity < HUMIDITY_MIN) return HUMIDITY_MIN;
    if (humidity > HUMIDITY_MAX) return HUMIDITY_MAX;
    return humidity;
}

static float validateLight(float light) {
    if (light < 0 || isnan(light)) return -1.0f;
    if (light < LIGHT_MIN) return LIGHT_MIN;
    if (light > LIGHT_MAX) return LIGHT_MAX;
    return light;
}

// ============================================================================
// FREERTOS TASKS
// ============================================================================

void TaskNetworking(void *pvParameters) {
    (void) pvParameters;
    
    #if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_add(NULL); // Subscribe this task to Watchdog Timer
    #endif
    
    for (;;) {
        taskCheckpoint(TASK_NETWORKING, "loop_start");
        
        // Handle WiFi connection / config portal
        systemSetTaskProgress(TASK_NETWORKING, "wifi_loop");
        wifiLoop();
        
        // Skip all network services when WiFi not connected
        if (wifiIsConnected()) {
            // Handle Telnet clients
            taskCheckpoint(TASK_NETWORKING, "telnet_loop");
            telnetLoop();
            
            // Handle OTA updates
            taskCheckpoint(TASK_NETWORKING, "ota_loop");
            otaLoop();
        }
        
        // Handle Netpie MQTT (connect timeout set to 5s in netpieSetup)
        taskCheckpoint(TASK_NETWORKING, "netpie_loop");
        netpieLoop();
        
        // Reset heartbeat after Netpie (connect can take up to 5s)
        taskCheckpoint(TASK_NETWORKING, "post_netpie");
        
        // Yield to IDLE task to prevent Task WDT accumulation timeout
        systemSetTaskProgress(TASK_NETWORKING, "yield_after_netpie");
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Handle Local MQTT (Pi) (connect timeout set to 5s in localMqttSetup)
        taskCheckpoint(TASK_NETWORKING, "local_mqtt_loop");
        localMqttLoop();
        
        // Reset heartbeat after Local MQTT (connect can take up to 5s)
        taskCheckpoint(TASK_NETWORKING, "post_local_mqtt");
        
        // Command Handling from Serial/Telnet is safe here or needs mutex?
        // Serial is hardware, Telnet is network. 
        // commandCheckSerial() uses Serial.read(), safe to poll here or in separate task.
        systemSetTaskProgress(TASK_NETWORKING, "command_serial");
        commandCheckSerial();
        taskCheckpoint(TASK_NETWORKING, "pump_test_tick");
        commandPumpTestTick();      // Auto-off pump test after 3 seconds (CLI)
        taskCheckpoint(TASK_NETWORKING, "hwtest_tick");
        localMqttHwTestTick();      // Auto-off pump test after duration (Web HW Test)
        
        // Publish Data if connected
        static unsigned long lastPublish = 0;
        if (millis() - lastPublish >= 2000) { // Throttled publish check
            lastPublish = millis();
            if (wifiIsConnected()) {
                if (netpieIsConnected()) {
                    taskCheckpoint(TASK_NETWORKING, "netpie_publish");
                    netpiePublishData(currentWaterTemp, currentAirTemp, currentHumidity, currentTds, currentLight, currentPh);
                }
                taskCheckpoint(TASK_NETWORKING, "local_publish");
                localMqttPublishData(currentWaterTemp, currentAirTemp, currentHumidity, currentTds, currentLight, currentPh);
                taskCheckpoint(TASK_NETWORKING, "post_publish");
            }
        }
        
        // Yield to other tasks
        systemSetTaskProgress(TASK_NETWORKING, "idle_yield");
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay to prevent WDT and allow other tasks
    }
}

void TaskSensors(void *pvParameters) {
    (void) pvParameters;
    
    #if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_add(NULL); // Subscribe this task to Watchdog Timer
    #endif
    
    for (;;) {
        taskCheckpoint(TASK_SENSORS, "loop_start");
        
        // Water Temp (OneWire is slow, blocking)
        if (systemGetSensorEnabled(SENSOR_WATER_TEMP)) {
            systemSetTaskProgress(TASK_SENSORS, "water_temp");
            float rawWaterTemp = tempRead();
            currentWaterTemp = validateTemperature(rawWaterTemp);
            tempLoop(); // Maintains sensor state if needed
        } else {
            currentWaterTemp = NAN; // Reset if disabled
        }
        
        // Air Temp & Humidity
        if (systemGetSensorEnabled(SENSOR_AIR_TEMP)) {
            systemSetTaskProgress(TASK_SENSORS, "air_temp_humidity");
            float rawAirTemp = dhtReadTemperature();
            float rawHumidity = dhtReadHumidity();
            currentAirTemp = validateTemperature(rawAirTemp);
            currentHumidity = validateHumidity(rawHumidity);
            dhtLoop();
        } else {
            currentAirTemp = NAN;
            currentHumidity = NAN;
        }
        
        // TDS (Average/Filtering)
        // tdsLoop() เก็บ sample ภายใน (เรียก tdsRead ให้แล้ว)
        // ดึงค่าจาก tdsRead เฉพาะตอนที่ buffer พร้อม
        if (systemGetSensorEnabled(SENSOR_TDS)) {
            systemSetTaskProgress(TASK_SENSORS, "tds");
            tdsLoop(currentWaterTemp);
            if (tdsIsReady()) {
                currentTds = validateTds(tdsGetLastValue());
            }
        } else {
            currentTds = -1;
        }
        
        // Light
        if (systemGetSensorEnabled(SENSOR_LIGHT)) {
            systemSetTaskProgress(TASK_SENSORS, "light");
            if (lightIsReady()) {
                currentLight = validateLight(lightRead());
            }
            lightLoop();
        } else {
            currentLight = -1;
        }
        
        // pH
        if (systemGetSensorEnabled(SENSOR_PH)) {
            systemSetTaskProgress(TASK_SENSORS, "ph");
            if (!isnan(currentWaterTemp)) {
                phSetTemperature(currentWaterTemp);
            }
            phLoop();
            if (phIsReady()) {
                currentPh = validatePh(phRead());
            }
        } else {
            currentPh = -1;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Run at 10Hz approx.
    }
}

void TaskControl(void *pvParameters) {
    (void) pvParameters;
    
    #if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_add(NULL); // Subscribe this task to Watchdog Timer
    #endif
    
    for (;;) {
        taskCheckpoint(TASK_CONTROL, "loop_start");

        // System Management (Button checks etc)
        systemSetTaskProgress(TASK_CONTROL, "system_loop");
        systemLoop();

        // Water circulation / refill controller
        systemSetTaskProgress(TASK_CONTROL, "water_system");
        waterSystemLoop();
        
        // Light Controller Schedule
        systemSetTaskProgress(TASK_CONTROL, "light_control");
        lightCtrlLoop();

        // Fish feeder schedule
        systemSetTaskProgress(TASK_CONTROL, "fish_feeder");
        fishFeederLoop();

        // Exhaust fan controller
        systemSetTaskProgress(TASK_CONTROL, "fan_control");
        fanCtrlLoop();
        
        // Automation Engine (Process State Machine)
        systemSetTaskProgress(TASK_CONTROL, "automator");
        automatorLoop();
        
        // Check task heartbeats (detect stuck tasks)
        systemSetTaskProgress(TASK_CONTROL, "task_health_check");
        if (!systemCheckTaskHealth()) {
            LOG_ERROR("Task stuck detected! Printing stack info...");
            systemPrintStackInfo();
        }
        
        // System Health / Heap Check
        systemSetTaskProgress(TASK_CONTROL, "system_health_check");
        if (!systemIsHealthy()) {
             LOG_ERROR("System unhealthy! Free heap: %lu", ESP.getFreeHeap());
        }
        
        systemSetTaskProgress(TASK_CONTROL, "idle_delay");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================================
// MAIN FUNCTIONS
// ============================================================================

#ifndef PIO_UNIT_TESTING
void setup() {
    // เริ่มต้น Serial
    Serial.begin(SERIAL_BAUD_RATE);
    
    LOG_MODULE_START("Aquaponics Sensor System");
    LOG_INFO("Firmware Version: %s", systemGetVersion());
    LOG_INFO("Build Date: %s %s", __DATE__, __TIME__);
    
    // Initialize system management
    systemInit();
    
    // Report if last reboot was caused by a stuck task
    systemReportLastCrash();
    
#if defined(ESP32) && WATCHDOG_ENABLED
    // ESP-IDF 5.x (Arduino 3.x) uses new WDT API with config struct
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000,
            .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  // Monitor all cores
            .trigger_panic = true
        };
        esp_task_wdt_init(&wdt_config);
    #else
        // Legacy API for ESP-IDF 4.x (Arduino 2.x)
        esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
    #endif
    esp_task_wdt_add(NULL); // Add main loop/current task
    LOG_INFO("Watchdog Timer enabled (%d seconds)", WATCHDOG_TIMEOUT_SEC);
#endif
    
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // เริ่มต้น WiFi (Non-Blocking)
    wifiSetup();
    
    // เริ่มต้น Telnet Server
    telnetSetup();
    
    // เริ่มต้น OTA
    otaSetup();
    
    // เริ่มต้น Services อื่นๆ
    netpieSetup();
    localMqttSetup();
    lightCtrlSetup();
    fishFeederSetup();
    fanCtrlSetup();
    
    // เริ่มต้นเซ็นเซอร์
    tdsSetup();
    dhtSetup();
    tempSetup();
    lightSetup();
    phSetup();
    
    // เริ่มต้นสมองกลอัตโนมัติ
    automatorSetup();
    waterSystemSetup();
    
    // Command Handler
    commandSetup();
    
    LOG_INFO("Starting FreeRTOS Tasks...");
    
    // Create Tasks (save handles for stack monitoring)
    // Core 0: WiFi/Network (Protocol stack runs here usually)
    // Core 1: Arduino Loop / Sensors / Control
    
    TaskHandle_t hNet = NULL, hSens = NULL, hCtrl = NULL;
    
    xTaskCreatePinnedToCore(
        TaskNetworking,   "Networking",   8192,  NULL,  1,  &hNet,   0 // Core 0
    );
    
    xTaskCreatePinnedToCore(
        TaskSensors,      "Sensors",      8192,  NULL,  1,  &hSens,  1 // Core 1
    );
    
    xTaskCreatePinnedToCore(
        TaskControl,      "Control",      4096,  NULL,  1,  &hCtrl,  1 // Core 1
    );
    
    // Register task handles for stack monitoring
    systemSetTaskHandle(TASK_NETWORKING, hNet);
    systemSetTaskHandle(TASK_SENSORS, hSens);
    systemSetTaskHandle(TASK_CONTROL, hCtrl);

    LOG_INFO("All modules initialized & Tasks started");
    LOG_MODULE_END("Aquaponics Sensor System");
}

void loop() {
    // Arduino Main Loop Task
    // ต้อง Feed Watchdog เพื่อป้องกันระบบรีสตาร์ท (สาเหตุที่เครื่องรีเซ็ตทุก 60 วิ)
    #if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_reset();
    #endif
    
    // Empty loop - tasks handle everything
    // Can be used for background low-priority work
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif // PIO_UNIT_TESTING