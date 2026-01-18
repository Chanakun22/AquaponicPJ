/**
 * @file dhtSensor.cpp
 * @brief Implementation สำหรับ DHT22 Sensor
 */

#include "dhtSensor.h"

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
    Serial.println(F("[DHT] Sensor initialized"));
}

float dhtReadTemperature(void) {
    return _dht.readTemperature();
}

float dhtReadHumidity(void) {
    return _dht.readHumidity();
}

void dhtLoop(void) {
    // ตรวจสอบเวลา (Non-blocking delay)
    if (millis() - _dhtLastReadTime >= DHT_READ_INTERVAL) {
        _dhtLastReadTime = millis();
        
        float humidity = dhtReadHumidity();
        float temperature = dhtReadTemperature();
        
        // ตรวจสอบค่าที่อ่านได้
        if (isnan(humidity) || isnan(temperature)) {
            Serial.println(F("[DHT] Failed to read from sensor!"));
            return;
        }
        
        // บันทึกค่าล่าสุด
        _lastTemperature = temperature;
        _lastHumidity = humidity;
        
        // แสดงผล
        // Serial.print(F("[DHT] Temperature: "));
        // Serial.print(temperature, 1);
        // Serial.print(F(" °C, Humidity: "));
        // Serial.print(humidity, 1);
        // Serial.println(F(" %"));
    }
}