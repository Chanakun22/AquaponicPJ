/**
 * @file tempSensor.cpp
 * @brief Implementation สำหรับ DS18B20 Temperature Sensor
 */

#include "tempSensor.h"


// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static OneWire _oneWire(ONE_WIRE_PIN);
static DallasTemperature _sensors(&_oneWire);
static unsigned long _tempLastReadTime = 0;
static float _lastWaterTemp = NAN;

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void tempSetup(void) {
    _sensors.begin();
    Serial.println(F("[TEMP] DS18B20 initialized"));
}

float tempRead(void) {
    _sensors.requestTemperatures();
    float temp = _sensors.getTempCByIndex(0);
    
    // ตรวจสอบค่าผิดพลาด (-127 = ไม่มีเซ็นเซอร์)
    if (temp == -127.0f) {
        return NAN;
    }
    
    return temp;
}

void tempLoop(void) {
    // ตรวจสอบเวลา (Non-blocking delay)
    if (millis() - _tempLastReadTime >= TEMP_READ_INTERVAL) {
        _tempLastReadTime = millis();
        
        float temperature = tempRead();
        
        // ตรวจสอบค่าที่อ่านได้
        if (isnan(temperature)) {
            Serial.println(F("[TEMP] Failed to read from sensor!"));
            return;
        }
        
        // บันทึกค่าล่าสุด
        _lastWaterTemp = temperature;
        
        // แสดงผล
        // Serial.print(F("[TEMP] Water Temperature: "));
        // Serial.print(temperature, 1);
        // Serial.println(F(" °C"));
    }
}
