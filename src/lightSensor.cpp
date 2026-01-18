/**
 * @file lightSensor.cpp
 * @brief Implementation สำหรับ BH1750 Light Sensor
 */

#include "lightSensor.h"
#include <Wire.h>
#include <BH1750.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static BH1750 _lightMeter;
static unsigned long _lightLastReadTime = 0;
static float _lastLux = -1;
static bool _sensorReady = false;

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void lightSetup(void) {
    Serial.println(F("[LIGHT] Initializing BH1750..."));
    
    // เริ่มต้น I2C ด้วย pins ที่กำหนด
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    
    // เริ่มต้น BH1750
    if (_lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, BH1750_ADDRESS)) {
        _sensorReady = true;
        Serial.println(F("[LIGHT] BH1750 initialized successfully"));
    } else {
        _sensorReady = false;
        Serial.println(F("[LIGHT] ERROR: BH1750 not found! Check wiring."));
    }
}

float lightRead(void) {
    if (!_sensorReady) {
        return -1.0f;
    }
    
    if (_lightMeter.measurementReady()) {
        _lastLux = _lightMeter.readLightLevel();
        
        // ตรวจสอบค่าผิดปกติ
        if (_lastLux < 0) {
            Serial.println(F("[LIGHT] Read error!"));
            return -1.0f;
        }
    }
    
    return _lastLux;
}

void lightLoop(void) {
    // ตรวจสอบเวลา (Non-blocking delay)
    if (millis() - _lightLastReadTime >= LIGHT_READ_INTERVAL) {
        _lightLastReadTime = millis();
        
        float lux = lightRead();
        
        if (_sensorReady && lux >= 0) {
            // Serial.print(F("[LIGHT] Illuminance: "));
            // Serial.print(lux, 1);
            // Serial.println(F(" lux"));
        }
    }
}

bool lightIsReady(void) {
    return _sensorReady;
}
