/**
 * @file lightSensor.cpp
 * @brief Implementation สำหรับ BH1750 Light Sensor
 */

#include "lightSensor.h"
#include "i2cBus.h"
#include "logger.h"
#include <BH1750.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static BH1750 _lightMeter;
static unsigned long _lightLastReadTime = 0;
static float _lastLux = -1;
static bool _sensorReady = false;

static void _readLightHardware(void) {
    if (!_sensorReady) {
        return;
    }

    if (!i2cBusLock()) {
        LOG_WARN("BH1750 I2C bus busy");
        return;
    }

    if (_lightMeter.measurementReady()) {
        float lux = _lightMeter.readLightLevel();

        // Keep the last good cache on transient I2C/read errors.
        if (lux < 0) {
            LOG_WARN("BH1750 read error");
        } else {
            _lastLux = lux;
        }
    }

    i2cBusUnlock();
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void lightSetup(void) {
    Serial.println(F("[LIGHT] Initializing BH1750..."));
    
    i2cBusSetup();
    
    // เริ่มต้น BH1750
    bool beginOk = false;
    if (i2cBusLock()) {
        beginOk = _lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, BH1750_ADDRESS);
        i2cBusUnlock();
    }

    if (beginOk) {
        _sensorReady = true;
        _lightLastReadTime = millis() - LIGHT_READ_INTERVAL; // force first task loop read
        LOG_INFO("BH1750 initialized successfully");
    } else {
        _sensorReady = false;
        LOG_ERROR("BH1750 not found! Check wiring.");
    }
}

float lightRead(void) {
    if (!_sensorReady) {
        return -1.0f;
    }

    return _lastLux;
}

void lightLoop(void) {
    // ตรวจสอบเวลา (Non-blocking delay)
    if (millis() - _lightLastReadTime >= LIGHT_READ_INTERVAL) {
        _lightLastReadTime = millis();
        
        _readLightHardware();
        
        if (_sensorReady && _lastLux >= 0) {
            // Serial.print(F("[LIGHT] Illuminance: "));
            // Serial.print(_lastLux, 1);
            // Serial.println(F(" lux"));
        }
    }
}

bool lightIsReady(void) {
    return _sensorReady;
}
