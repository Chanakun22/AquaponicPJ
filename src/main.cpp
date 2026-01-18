/**
 * @file main.cpp
 * @brief โปรแกรมหลักสำหรับระบบ Aquaponics Sensor
 * @details รวมเซ็นเซอร์ TDS, DHT22, DS18B20, BH1750 พร้อม WiFi, NETPIE และ Light Control
 */

#include <Arduino.h>
#include "config.h"
#include "logger.h"
#include "system.h"
#include "ota.h"
#include "webServer.h"
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

/**
 * @brief ตรวจสอบค่า TDS ว่าอยู่ในช่วงที่ยอมรับได้
 * @param tds ค่า TDS ที่ต้องการตรวจสอบ
 * @return ค่า TDS ที่ validate แล้ว หรือ -1 ถ้าผิดปกติ
 */
static float validateTds(float tds) {
    if (tds < 0 || isnan(tds)) {
        return -1.0f;
    }
    if (tds < TDS_MIN) return TDS_MIN;
    if (tds > TDS_MAX) return TDS_MAX;
    return tds;
}

/**
 * @brief ตรวจสอบค่า pH ว่าอยู่ในช่วงที่ยอมรับได้
 * @param ph ค่า pH ที่ต้องการตรวจสอบ
 * @return ค่า pH ที่ validate แล้ว หรือ -1 ถ้าผิดปกติ
 */
static float validatePh(float ph) {
    if (ph < 0 || isnan(ph)) {
        return -1.0f;
    }
    if (ph < PH_MIN) return PH_MIN;
    if (ph > PH_MAX) return PH_MAX;
    return ph;
}

/**
 * @brief ตรวจสอบอุณหภูมิว่าอยู่ในช่วงที่ยอมรับได้
 * @param temp อุณหภูมิที่ต้องการตรวจสอบ
 * @return อุณหภูมิที่ validate แล้ว หรือ NAN ถ้าผิดปกติ
 */
static float validateTemperature(float temp) {
    if (isnan(temp)) {
        return NAN;
    }
    if (temp < TEMP_MIN || temp > TEMP_MAX) {
        return NAN;  // ค่าผิดปกติ return NAN
    }
    return temp;
}

/**
 * @brief ตรวจสอบความชื้นว่าอยู่ในช่วงที่ยอมรับได้
 * @param humidity ความชื้นที่ต้องการตรวจสอบ
 * @return ความชื้นที่ validate แล้ว หรือ NAN ถ้าผิดปกติ
 */
static float validateHumidity(float humidity) {
    if (isnan(humidity)) {
        return NAN;
    }
    if (humidity < HUMIDITY_MIN) return HUMIDITY_MIN;
    if (humidity > HUMIDITY_MAX) return HUMIDITY_MAX;
    return humidity;
}

/**
 * @brief ตรวจสอบความเข้มแสงว่าอยู่ในช่วงที่ยอมรับได้
 * @param light ความเข้มแสงที่ต้องการตรวจสอบ
 * @return ความเข้มแสงที่ validate แล้ว หรือ -1 ถ้าผิดปกติ
 */
static float validateLight(float light) {
    if (light < 0 || isnan(light)) {
        return -1.0f;
    }
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
    delay(100);  // Allow serial to stabilize
    
    LOG_MODULE_START("Aquaponics Sensor System");
    LOG_INFO("Firmware Version: %s", systemGetVersion());
    LOG_INFO("Build Date: %s %s", __DATE__, __TIME__);
    
    // Initialize system management first
    systemInit();
    
#if defined(ESP32) && WATCHDOG_ENABLED
    // เริ่มต้น Watchdog Timer
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);  // true = panic on timeout (reboot)
    esp_task_wdt_add(NULL);  // Add current task to watchdog
    LOG_INFO("Watchdog Timer enabled (%d seconds)", WATCHDOG_TIMEOUT_SEC);
#endif
    
    // เริ่มต้น LED สถานะ
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // เริ่มต้น WiFi (non-blocking)
    wifiSetup();
    
    // เริ่มต้น Web Server & WebSerial
    webServerSetup();
    
    // เริ่มต้น OTA (after WiFi)
    otaSetup();
    
    // เริ่มต้น NETPIE
    netpieSetup();
    
    // เริ่มต้น Light Controller
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
    
    // ======== OTA Update Handler ========
    otaLoop();
    
    // ======== WiFi & NETPIE (ทำงานเมื่อออนไลน์) ========
    wifiLoop();
    netpieLoop();
    
    // ======== Light Controller (ตรวจสอบตารางเวลา) ========
    lightCtrlLoop();
    
    // ======== เซ็นเซอร์ (ทำงานเสมอ) ========
    
    // อ่านอุณหภูมิน้ำ + Validate
    float rawWaterTemp = tempRead();
    currentWaterTemp = validateTemperature(rawWaterTemp);
    tempLoop();
    
    // อ่าน DHT22 + Validate
    float rawAirTemp = dhtReadTemperature();
    float rawHumidity = dhtReadHumidity();
    currentAirTemp = validateTemperature(rawAirTemp);
    currentHumidity = validateHumidity(rawHumidity);
    dhtLoop();
    
    // อ่าน TDS (ใช้อุณหภูมิน้ำชดเชย) + Validate
    if (tdsIsReady()) {
        float rawTds = tdsRead(isnan(currentWaterTemp) ? 25.0 : currentWaterTemp);
        currentTds = validateTds(rawTds);
    }
    tdsLoop(isnan(currentWaterTemp) ? 25.0 : currentWaterTemp);
    
    // อ่าน BH1750 Light Sensor + Validate
    if (lightIsReady()) {
        float rawLight = lightRead();
        currentLight = validateLight(rawLight);
    }
    lightLoop();
    
    // อ่าน pH Sensor (ใช้อุณหภูมิน้ำชดเชย) + Validate
    if (!isnan(currentWaterTemp)) {
        phSetTemperature(currentWaterTemp);  // ส่งอุณหภูมิให้ pH sensor
    }
    if (phIsReady()) {
        float rawPh = phRead();
        currentPh = validatePh(rawPh);
    }
    phLoop();
    
    // ======== Serial Commands ========
    if (Serial.available()) {
        char cmd[16];  // Buffer สำหรับ command (ยาวสุด 15 ตัวอักษร)
        size_t len = Serial.readBytesUntil('\n', cmd, sizeof(cmd) - 1);
        cmd[len] = '\0';
        
        // Trim whitespace
        while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\r' || cmd[len-1] == '\n')) {
            cmd[--len] = '\0';
        }
        
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
            LOG_INFO("Uptime: %lu seconds", health.uptimeMs / 1000);
            LOG_INFO("Free Heap: %lu bytes", health.freeHeap);
            LOG_INFO("Min Free Heap: %lu bytes", health.minFreeHeap);
            LOG_INFO("Watchdog Resets: %u", health.watchdogResets);
            LOG_INFO("WiFi Reconnects: %u", health.wifiReconnects);
            LOG_INFO("MQTT Reconnects: %u", health.mqttReconnects);
            LOG_INFO("Sensors OK: %s", health.sensorsOk ? "YES" : "NO");
            LOG_INFO("=========================");
        } else if (strcmp(cmd, "reset") == 0) {
            LOG_WARN("Factory reset requested!");
            systemFactoryReset();
        } else if (strcmp(cmd, "help") == 0) {
            LOG_INFO("===== COMMANDS =====");
            LOG_INFO("cal7   - Calibrate pH 7.0");
            LOG_INFO("cal4   - Calibrate pH 4.0");
            LOG_INFO("ph     - Show current pH");
            LOG_INFO("health - Show system health");
            LOG_INFO("reset  - Factory reset");
            LOG_INFO("====================");
        } else {
            LOG_WARN("Unknown command: %s (type 'help' for commands)", cmd);
        }
    }
    
    // ======== ส่งข้อมูลไป NETPIE (เฉพาะเมื่อ connected) ========
    if (wifiIsConnected() && netpieIsConnected()) {
        netpiePublishData(currentWaterTemp, currentAirTemp, currentHumidity, currentTds, currentLight, currentPh);
    }
    
    // ======== System Health Check ========
    if (!systemIsHealthy()) {
        #if defined(ESP32)
        LOG_ERROR("System unhealthy! Free heap: %lu bytes", ESP.getFreeHeap());
        #endif
    }
    
#if defined(ESP32) && WATCHDOG_ENABLED
    // Reset Watchdog Timer (feed watchdog)
    // เรียกทุกครั้งที่ loop() รันจบ เพื่อป้องกัน timeout
    esp_task_wdt_reset();
#endif
}