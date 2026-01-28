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
// MAIN FUNCTIONS
// ============================================================================

void setup() {
    // เริ่มต้น Serial
    Serial.begin(SERIAL_BAUD_RATE);
    delay(100);
    
    LOG_MODULE_START("Aquaponics Sensor System");
    LOG_INFO("Firmware Version: %s", systemGetVersion());
    LOG_INFO("Build Date: %s %s", __DATE__, __TIME__);
    
    // Initialize system management
    systemInit();
    
#if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    LOG_INFO("Watchdog Timer enabled (%d seconds)", WATCHDOG_TIMEOUT_SEC);
#endif
    
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // เริ่มต้น WiFi (Non-Blocking)
    wifiSetup();
    
    // เริ่มต้น Telnet Server (สำหรับ Debug ผ่าน WiFi)
    telnetSetup();
    
    // เริ่มต้น OTA
    otaSetup();
    
    // เริ่มต้น Services อื่นๆ
    netpieSetup();
    lightCtrlSetup();
    
    // เริ่มต้นเซ็นเซอร์
    tdsSetup();
    dhtSetup();
    tempSetup();
    lightSetup();
    phSetup();
    
    LOG_INFO("All modules initialized");
    LOG_MODULE_END("Aquaponics Sensor System");
}

void loop() {
    // ======== System Management ========
    systemLoop();
    
    // ======== Network Services ========
    wifiLoop();      // Handle WiFi connection / config portal
    telnetLoop();    // Handle Telnet clients
    otaLoop();       // Handle OTA updates
    netpieLoop();    // Handle Netpie MQTT
    
    // ======== Light Controller ========
    lightCtrlLoop();
    
    // ======== Sensors ========
    
    // Water Temp
    float rawWaterTemp = tempRead();
    currentWaterTemp = validateTemperature(rawWaterTemp);
    tempLoop();
    
    // Air Temp & Humidity
    float rawAirTemp = dhtReadTemperature();
    float rawHumidity = dhtReadHumidity();
    currentAirTemp = validateTemperature(rawAirTemp);
    currentHumidity = validateHumidity(rawHumidity);
    dhtLoop();
    
    // TDS (compensate with water temp)
    if (tdsIsReady()) {
        float rawTds = tdsRead(isnan(currentWaterTemp) ? 25.0 : currentWaterTemp);
        currentTds = validateTds(rawTds);
    }
    tdsLoop(isnan(currentWaterTemp) ? 25.0 : currentWaterTemp);
    
    // Light
    if (lightIsReady()) {
        float rawLight = lightRead();
        currentLight = validateLight(rawLight);
    }
    lightLoop();
    
    // pH (compensate with water temp)
    if (!isnan(currentWaterTemp)) {
        phSetTemperature(currentWaterTemp);
    }
    if (phIsReady()) {
        float rawPh = phRead();
        currentPh = validatePh(rawPh);
    }
    phLoop();
    
    // ======== Serial Commands ========
    if (Serial.available()) {
        char cmd[16];
        size_t len = Serial.readBytesUntil('\n', cmd, sizeof(cmd) - 1);
        cmd[len] = '\0';
        while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\r' || cmd[len-1] == '\n')) cmd[--len] = '\0';
        
        if (strcmp(cmd, "cal7") == 0) {
            LOG_INFO("Calibrating pH 7.0...");
            phCalibratePh7();
        } else if (strcmp(cmd, "cal4") == 0) {
            LOG_INFO("Calibrating pH 4.0...");
            phCalibratePh4();
        } else if (strcmp(cmd, "ph") == 0) {
            LOG_INFO("pH: %.2f, Voltage: %.1f mV", phRead(), phReadVoltage());
        } else if (strcmp(cmd, "health") == 0) {
            SystemHealth_t health;
            systemGetHealth(&health);
            LOG_INFO("===== SYSTEM HEALTH =====");
            LOG_INFO("Uptime: %lu s", health.uptimeMs / 1000);
            LOG_INFO("Free Heap: %lu B", health.freeHeap);
            LOG_INFO("Min Free Heap: %lu B", health.minFreeHeap);
            LOG_INFO("Watchdog Resets: %u", health.watchdogResets);
            LOG_INFO("WiFi Reconnects: %u", health.wifiReconnects);
            LOG_INFO("=========================");
        } else if (strcmp(cmd, "reset") == 0) {
            LOG_WARN("Factory reset requested!");
            systemFactoryReset();
        } else if (strcmp(cmd, "reboot") == 0) {
            LOG_WARN("Rebooting...");
            ESP.restart();
        } else {
            LOG_WARN("Unknown command: %s", cmd);
        }
    }
    
    // ======== Publish Data ========
    if (wifiIsConnected() && netpieIsConnected()) {
        netpiePublishData(currentWaterTemp, currentAirTemp, currentHumidity, currentTds, currentLight, currentPh);
    }
    
    // ======== Health Check ========
    if (!systemIsHealthy()) {
        #if defined(ESP32)
        LOG_ERROR("System unhealthy! Free heap: %lu", ESP.getFreeHeap());
        #endif
    }
    
#if defined(ESP32) && WATCHDOG_ENABLED
    esp_task_wdt_reset();
#endif
}