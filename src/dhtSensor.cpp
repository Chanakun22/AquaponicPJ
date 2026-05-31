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
static unsigned long _dhtBackoffUntil = 0;
static float _lastTemperature = NAN;
static float _lastHumidity = NAN;
static uint8_t _dhtFailureCount = 0;

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void dhtSetup(void) {
    _dht.begin();
    _dhtLastReadTime = millis() - DHT_READ_INTERVAL; // force first read in dhtLoop()
    _dhtBackoffUntil = 0;
    _dhtFailureCount = 0;
    _lastTemperature = NAN;
    _lastHumidity = NAN;
    LOG_INFO("DHT22 sensor initialized");
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
    unsigned long currentTime = millis();
    if (_dhtBackoffUntil != 0 && currentTime < _dhtBackoffUntil) {
        return;
    }

    // ตรวจสอบเวลา (Non-blocking delay)
    if (currentTime - _dhtLastReadTime >= DHT_READ_INTERVAL) {
        _dhtLastReadTime = currentTime;
        
        // Read new values
        unsigned long readStart = millis();
        float humidity = _dht.readHumidity();
        float temperature = _dht.readTemperature();
        unsigned long readElapsed = millis() - readStart;
        if (readElapsed > DHT_SLOW_READ_WARN_MS) {
            LOG_WARN("DHT22 read took %lu ms", readElapsed);
        }
        
        // ตรวจสอบค่าที่อ่านได้ (Nano-second check logic handled by library, but if NAN we keep old value or update?)
        // Standard practice: if read fails (NAN), keep old value OR return NAN.
        // But since we use cached value for main loop, let's only update if valid.
        
        if (isnan(humidity) || isnan(temperature)) {
            _dhtFailureCount++;
            _lastTemperature = NAN;
            _lastHumidity = NAN;

            if (_dhtFailureCount >= DHT_MAX_CONSECUTIVE_FAILURES) {
                _dhtBackoffUntil = currentTime + DHT_FAIL_BACKOFF_MS;
                LOG_WARN("DHT22 read failed %u times; backing off for %lu ms",
                         _dhtFailureCount, (unsigned long)DHT_FAIL_BACKOFF_MS);
                _dhtFailureCount = 0;
            }
        } else {
            // บันทึกค่าล่าสุด
            _lastTemperature = temperature;
            _lastHumidity = humidity;
            _dhtFailureCount = 0;
            _dhtBackoffUntil = 0;
        }
    }
}