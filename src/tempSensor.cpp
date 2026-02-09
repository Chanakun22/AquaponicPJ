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

// DS18B20 conversion time (750ms for 12-bit resolution) + safety margin
static const unsigned long CONVERSION_DELAY_MS = 800;
static uint8_t _retryCount = 0;
const uint8_t MAX_RETRIES = 3;

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
                _retryCount = 0; // Reset retry counter on new cycle
            }
            break;
            
        case TEMP_WAITING:
            // รอ conversion เสร็จ (800ms สำหรับ 12-bit including margin)
            if (currentTime - _tempRequestTime >= CONVERSION_DELAY_MS) {
                float temp = _sensors.getTempCByIndex(0);
                
                // ตรวจสอบค่าผิดพลาด (-127 = ไม่มีเซ็นเซอร์ หรือ Error)
                if (temp == -127.0f || temp == 85.0f) { // 85.0 is also power-on reset value
                    if (_retryCount < MAX_RETRIES) {
                        _retryCount++;
                        LOG_WARN("DS18B20 read fail (%.1f), retrying... (%d/%d)", temp, _retryCount, MAX_RETRIES);
                        
                        // Request again immediately
                        _sensors.requestTemperatures();
                        _tempRequestTime = currentTime;
                        // Stay in TEMP_WAITING
                        return; 
                    } else {
                        LOG_ERROR("Failed to read temperature from DS18B20 after %d retries", MAX_RETRIES);
                        _lastWaterTemp = NAN;
                    }
                } else {
                    _lastWaterTemp = temp;
                    // LOG_INFO("Temp read success: %.2f", temp);
                }
                
                _tempLastReadTime = currentTime;
                _tempState = TEMP_IDLE;
            }
            break;
    }
}
