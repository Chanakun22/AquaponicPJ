/**
 * @file dhtSensor.cpp
 * @brief Implementation สำหรับ DHT22 Sensor
 */

#include "dhtSensor.h"
#include "logger.h"

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static DHT _dht(DHT_PIN, DHT_TYPE);
static unsigned long _dhtLastReadTime = 0;
static float _lastTemperature = NAN;
static float _lastHumidity = NAN;

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void dhtSetup(void) {
    _dht.begin();
    LOG_INFO("DHT22 sensor initialized");
    // Initial read (might be NAN, but better than nothing)
    _lastTemperature = _dht.readTemperature();
    _lastHumidity = _dht.readHumidity();
}

float dhtReadTemperature(void) {
    // Return cached value
    return _lastTemperature;
}

float dhtReadHumidity(void) {
    // Return cached value
    return _lastHumidity;
}

void dhtLoop(void) {
    // ตรวจสอบเวลา (Non-blocking delay)
    if (millis() - _dhtLastReadTime >= DHT_READ_INTERVAL) {
        _dhtLastReadTime = millis();
        
        // Read new values
        float humidity = _dht.readHumidity();
        float temperature = _dht.readTemperature();
        
        // ตรวจสอบค่าที่อ่านได้ (Nano-second check logic handled by library, but if NAN we keep old value or update?)
        // Standard practice: if read fails (NAN), keep old value OR return NAN.
        // But since we use cached value for main loop, let's only update if valid.
        
        if (isnan(humidity) || isnan(temperature)) {
             // LOG_WARN("Failed to read from DHT22 sensor");
             // Don't update cache if failed, so system sees last known good value? 
             // Or update to NAN to indicate error?
             // Let's update to NAN so we know it's failing.
             _lastTemperature = NAN;
             _lastHumidity = NAN;
        } else {
            // บันทึกค่าล่าสุด
            _lastTemperature = temperature;
            _lastHumidity = humidity;
        }
    }
}