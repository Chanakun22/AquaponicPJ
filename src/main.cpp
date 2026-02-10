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
#include "wifiConn.h"
#include "netpie.h"
#include "localMqtt.h"
#include "commandHandler.h"


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
    
    // Setup (Already called in setup(), but good to ensure readiness)
    
    for (;;) {
        // Handle WiFi connection / config portal
        wifiLoop();
        
        // Handle Telnet clients
        telnetLoop();
        
        // Handle OTA updates
        otaLoop();
        
        // Handle Netpie MQTT
        netpieLoop();
        
        // Handle Local MQTT (Pi)
        localMqttLoop();
        
        // Command Handling from Serial/Telnet is safe here or needs mutex?
        // Serial is hardware, Telnet is network. 
        // commandCheckSerial() uses Serial.read(), safe to poll here or in separate task.
        commandCheckSerial();
        
        // Publish Data if connected
        static unsigned long lastPublish = 0;
        if (millis() - lastPublish >= 2000) { // Throttled publish check
            lastPublish = millis();
            if (wifiIsConnected()) {
                if (netpieIsConnected()) {
                    netpiePublishData(currentWaterTemp, currentAirTemp, currentHumidity, currentTds, currentLight, currentPh);
                }
                localMqttPublishData(currentWaterTemp, currentAirTemp, currentHumidity, currentTds, currentLight, currentPh);
            }
        }
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay to prevent WDT and allow other tasks
    }
}

void TaskSensors(void *pvParameters) {
    (void) pvParameters;
    
    for (;;) {
        // Water Temp (OneWire is slow, blocking)
        if (systemGetSensorEnabled(SENSOR_WATER_TEMP)) {
            float rawWaterTemp = tempRead();
            currentWaterTemp = validateTemperature(rawWaterTemp);
            tempLoop(); // Maintains sensor state if needed
        } else {
            currentWaterTemp = NAN; // Reset if disabled
        }
        
        // Air Temp & Humidity
        if (systemGetSensorEnabled(SENSOR_AIR_TEMP)) {
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
            float tempForTds = !isnan(currentWaterTemp) ? currentWaterTemp : 25.0f;
            tdsLoop(tempForTds);
            if (tdsIsReady()) {
                currentTds = validateTds(tdsGetLastValue());
            }
        } else {
            currentTds = -1;
        }
        
        // Light
        if (systemGetSensorEnabled(SENSOR_LIGHT)) {
            if (lightIsReady()) {
                currentLight = validateLight(lightRead());
            }
            lightLoop();
        } else {
            currentLight = -1;
        }
        
        // pH
        if (systemGetSensorEnabled(SENSOR_PH)) {
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
        
        // Health Check & Watchdog Reset (if this task is monitored)
        #if defined(ESP32) && WATCHDOG_ENABLED
        esp_task_wdt_reset();
        #endif
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Run at 10Hz approx.
    }
}

void TaskControl(void *pvParameters) {
    (void) pvParameters;
    
    for (;;) {
        // System Management (Button checks etc)
        systemLoop();
        
        // Light Controller Schedule
        lightCtrlLoop();
        
        // System Health / Heap Check
        if (!systemIsHealthy()) {
             LOG_ERROR("System unhealthy! Free heap: %lu", ESP.getFreeHeap());
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================================
// MAIN FUNCTIONS
// ============================================================================

void setup() {
    // เริ่มต้น Serial
    Serial.begin(SERIAL_BAUD_RATE);
    
    LOG_MODULE_START("Aquaponics Sensor System");
    LOG_INFO("Firmware Version: %s", systemGetVersion());
    LOG_INFO("Build Date: %s %s", __DATE__, __TIME__);
    
    // Initialize system management
    systemInit();
    
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
    
    // เริ่มต้นเซ็นเซอร์
    tdsSetup();
    dhtSetup();
    tempSetup();
    lightSetup();
    phSetup();
    
    // Command Handler
    commandSetup();
    
    LOG_INFO("Starting FreeRTOS Tasks...");
    
    // Create Tasks
    // Core 0: WiFi/Network (Protocol stack runs here usually)
    // Core 1: Arduino Loop / Sensors / Control
    
    xTaskCreatePinnedToCore(
        TaskNetworking,   "Networking",   8192,  NULL,  1,  NULL,  0 // Core 0
    );
    
    xTaskCreatePinnedToCore(
        TaskSensors,      "Sensors",      8192,  NULL,  1,  NULL,  1 // Core 1
    );
    
    xTaskCreatePinnedToCore(
        TaskControl,      "Control",      4096,  NULL,  1,  NULL,  1 // Core 1
    );

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