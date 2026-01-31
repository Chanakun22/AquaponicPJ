/**
 * @file tempSensor.cpp
 * @brief Implementation สำหรับ DS18B20 Temperature Sensor
 * @details ใช้ Async mode เพื่อให้เป็น Non-Blocking
 */

#include "tempSensor.h"
#include "logger.h"


// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static OneWire _oneWire(ONE_WIRE_PIN);
static DallasTemperature _sensors(&_oneWire);
static unsigned long _tempLastReadTime = 0;
static unsigned long _tempRequestTime = 0;
static float _lastWaterTemp = NAN;

// State machine for async read
enum TempState { TEMP_IDLE, TEMP_WAITING };
static TempState _tempState = TEMP_IDLE;

// DS18B20 conversion time (750ms for 12-bit resolution)
static const unsigned long CONVERSION_DELAY_MS = 750;

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void tempSetup(void) {
    _sensors.begin();
    
    // ⭐ เปิด Async mode - ไม่ต้องรอ conversion
    _sensors.setWaitForConversion(false);
    
    // Check if sensor is present
    int deviceCount = _sensors.getDeviceCount();
    if (deviceCount == 0) {
        LOG_ERROR("DS18B20 not found! Check wiring.");
    } else {
        LOG_INFO("DS18B20 initialized - Found %d device(s) [Async Mode]", deviceCount);
    }
}

float tempRead(void) {
    return _lastWaterTemp;
}

void tempLoop(void) {
    unsigned long currentTime = millis();
    
    switch (_tempState) {
        case TEMP_IDLE:
            // ถึงเวลาอ่านค่าใหม่หรือยัง?
            if (currentTime - _tempLastReadTime >= TEMP_READ_INTERVAL) {
                // ⭐ สั่ง request - ไม่ block เพราะ setWaitForConversion(false)
                _sensors.requestTemperatures();
                _tempRequestTime = currentTime;
                _tempState = TEMP_WAITING;
            }
            break;
            
        case TEMP_WAITING:
            // รอ conversion เสร็จ (750ms สำหรับ 12-bit)
            if (currentTime - _tempRequestTime >= CONVERSION_DELAY_MS) {
                float temp = _sensors.getTempCByIndex(0);
                
                // ตรวจสอบค่าผิดพลาด (-127 = ไม่มีเซ็นเซอร์)
                if (temp == -127.0f) {
                    LOG_WARN("Failed to read temperature from DS18B20");
                    _lastWaterTemp = NAN;
                } else {
                    _lastWaterTemp = temp;
                }
                
                _tempLastReadTime = currentTime;
                _tempState = TEMP_IDLE;
            }
            break;
    }
}
