/**
 * @file TdsSensor.cpp
 * @brief Implementation สำหรับ TDS Sensor พร้อม 2-Point Calibration + NVS Storage
 */

#include "TdsSensor.h"
#include "logger.h"
#include "config.h"
#include <Preferences.h>
#include <math.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static Preferences _tdsPrefs;
static unsigned long _tdsLastReadTime = 0;
static int _tdsBuffer[TDS_SAMPLE_COUNT];
static int _tdsBufferIndex = 0;
static int _tdsSampleCollected = 0;  // จำนวน sample ที่เก็บได้
static bool _tdsReady = false;       // flag บอกว่าเก็บ sample ครบหรือยัง
static float _tdsLastResult = -1.0f; // ค่า TDS ล่าสุดจาก tdsRead()
static float _tdsLastVoltage = -1.0f;
static float _tdsVoltageMovingAverage = -1.0f;
static float _tdsMovingAverage = -1.0f;

#if defined(ESP32)
static portMUX_TYPE _tdsMux = portMUX_INITIALIZER_UNLOCKED;
#define TDS_LOCK() portENTER_CRITICAL(&_tdsMux)
#define TDS_UNLOCK() portEXIT_CRITICAL(&_tdsMux)
#else
#define TDS_LOCK()
#define TDS_UNLOCK()
#endif

// Calibration variables
static float _kValue = 1.0f;      // Gain factor (scaling)
static float _offset = 0.0f;      // Offset factor (shifting)

// Raw Calibration Data (Stored in NVS)
static bool _isCalibrated = false;
static float _calLowPpm = 0.0f;
static float _calLowVoltage = 0.0f;
static float _calHighPpm = 0.0f;
static float _calHighVoltage = 0.0f;

// Standard DFRobot Polynomial for TDS (ppm) from Voltage (V)
// Relation: TDS = (133.42*v^3 - 255.86*v^2 + 857.39*v) * 0.5
static float _getStandardTds(float voltage) {
    if (voltage <= 0) return 0;
    return (133.42f * voltage * voltage * voltage 
          - 255.86f * voltage * voltage 
          + 857.39f * voltage) * 0.5f;
}

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

static void _calculateCalibrationFactors() {
    if (_calHighVoltage == _calLowVoltage) {
        _kValue = 1.0f;
        _offset = 0.0f;
        return;
    }
    
    // Calculate expected base TDS from voltages
    float baseLow = _getStandardTds(_calLowVoltage);
    float baseHigh = _getStandardTds(_calHighVoltage);
    
    // Calculate K and Offset to map Base -> Actual
    // Actual = K * Base + Offset
    _kValue = (_calHighPpm - _calLowPpm) / (baseHigh - baseLow);
    _offset = _calLowPpm - (_kValue * baseLow);
    
    LOG_INFO("Calibrated K: %.4f, Offset: %.4f", _kValue, _offset);
}

static void _loadCalibrationFromNVS() {
    _tdsPrefs.begin("tdsSensor", true);
    _isCalibrated = _tdsPrefs.getBool("calibrated", false);
    if (_isCalibrated) {
        _calLowPpm = _tdsPrefs.getFloat("lowPpm", 0);
        _calLowVoltage = _tdsPrefs.getFloat("lowV", 0);
        _calHighPpm = _tdsPrefs.getFloat("highPpm", 0);
        _calHighVoltage = _tdsPrefs.getFloat("highV", 0);
        _calculateCalibrationFactors(); // Recalculate K/Offset
    }
    _tdsPrefs.end();
}

static void _saveCalibrationToNVS() {
    _tdsPrefs.begin("tdsSensor", false);
    _tdsPrefs.putBool("calibrated", _isCalibrated);
    _tdsPrefs.putFloat("lowPpm", _calLowPpm);
    _tdsPrefs.putFloat("lowV", _calLowVoltage);
    _tdsPrefs.putFloat("highPpm", _calHighPpm);
    _tdsPrefs.putFloat("highV", _calHighVoltage);
    _tdsPrefs.end();
    LOG_INFO("TDS Calibration saved to NVS");
}

// Median Filtering Algorithm
static int _getMedian(int* bArray, int iFilterLen) {
    int bTab[iFilterLen];
    for (byte i = 0; i < iFilterLen; i++)
        bTab[i] = bArray[i];
        
    int i, j, bTemp;
    for (j = 0; j < iFilterLen - 1; j++) {
        for (i = 0; i < iFilterLen - j - 1; i++) {
            if (bTab[i] > bTab[i + 1]) {
                bTemp = bTab[i];
                bTab[i] = bTab[i + 1];
                bTab[i + 1] = bTemp;
            }
        }
    }
    
    if ((iFilterLen & 1) > 0)
        bTemp = bTab[(iFilterLen - 1) / 2];
    else
        bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
        
    return bTemp;
}

static float _calculateTdsFromVoltage(float voltage) {
    // 1. Get Base TDS from Standard Polynomial
    float baseTds = _getStandardTds(voltage);
    
    // 2. Apply Calibration Adjustment
    if (_isCalibrated) {
        return (_kValue * baseTds) + _offset;
    } else {
        return baseTds;
    }
}

static float _calculateFilteredVoltage(void) {
    int tempBuffer[TDS_SAMPLE_COUNT];
    bool ready = false;

    TDS_LOCK();
    ready = _tdsReady;
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
        tempBuffer[i] = _tdsBuffer[i];
    }
    TDS_UNLOCK();

    if (!ready) {
        return -1.0f;
    }

    float rawVoltage = _getMedian(tempBuffer, TDS_SAMPLE_COUNT) * TDS_VREF / TDS_ADC_RESOLUTION;

    if (_tdsVoltageMovingAverage < 0) {
        _tdsVoltageMovingAverage = rawVoltage;
    } else {
        _tdsVoltageMovingAverage = (rawVoltage * 0.15f) + (_tdsVoltageMovingAverage * 0.85f);
    }

    return _tdsVoltageMovingAverage;
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void tdsSetup(void) {
    pinMode(TDS_PIN, INPUT);
    
    // โหลด calibration จาก NVS
    _loadCalibrationFromNVS();
    
    // เริ่มต้นค่า buffer ให้เป็น 0
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
        _tdsBuffer[i] = 0;
    }
    _tdsBufferIndex = 0;
    _tdsSampleCollected = 0;
    _tdsReady = false;
    _tdsLastResult = -1.0f;
    _tdsLastVoltage = -1.0f;
    _tdsVoltageMovingAverage = -1.0f;
    _tdsMovingAverage = -1.0f;
    
    LOG_INFO("TDS sensor initialized, collecting %d samples...", TDS_SAMPLE_COUNT);
    
    if (_isCalibrated) {
        LOG_INFO("TDS Calibration loaded from NVS: Low=%.0f ppm @ %.3fV, High=%.0f ppm @ %.3fV", 
                 _calLowPpm, _calLowVoltage, _calHighPpm, _calHighVoltage);
    } else {
        LOG_WARN("TDS not calibrated, using default formula");
    }
}

float tdsGetVoltage(void) {
    float voltage = -1.0f;
    TDS_LOCK();
    voltage = _tdsLastVoltage;
    TDS_UNLOCK();
    return voltage;
}

bool tdsIsReady(void) {
    bool ready = false;
    TDS_LOCK();
    ready = _tdsReady;
    TDS_UNLOCK();
    return ready;
}

bool tdsIsCalibrated(void) {
    return _isCalibrated;
}



float tdsRead(float temperature) {
    int sample = analogRead(TDS_PIN);
    bool becameReady = false;

    TDS_LOCK();
    _tdsBuffer[_tdsBufferIndex] = sample;
    _tdsBufferIndex = (_tdsBufferIndex + 1) % TDS_SAMPLE_COUNT;

    if (!_tdsReady && _tdsBufferIndex == 0) {
        _tdsReady = true;
        becameReady = true;
    }
    TDS_UNLOCK();

    if (becameReady) {
        LOG_INFO("TDS buffer full, sensors ready!");
    }

    if (!tdsIsReady()) return -1.0f;

    float voltage = _calculateFilteredVoltage();
    TDS_LOCK();
    _tdsLastVoltage = voltage;
    TDS_UNLOCK();

    if (voltage < 0.0f) return -1.0f;
    
    // Hardware Validation: Detect unplugged sensor (near 0V) or short circuit (near VREF)
    if (voltage < 0.05f || voltage > (TDS_VREF - 0.05f)) {
        TDS_LOCK();
        _tdsLastResult = NAN;
        TDS_UNLOCK();
        return NAN;
    }
    
    // --- Advanced Temperature Compensation ---
    // Standard linear factor is 1/(1+0.02(T-25))
    // Optimized for nutrient solution (approx 0.019 coeff)
    if (isnan(temperature)) {
        _tdsLastResult = NAN;
        return NAN;
    }
    float tempP = temperature; // Copy to avoid modifying param directly if needed.
    
    float compensationCoefficient = 1.0f + 0.019f * (tempP - 25.0f);
    float compensationVoltage = voltage / compensationCoefficient;
    
    float tdsValue = _calculateTdsFromVoltage(compensationVoltage);
    
    // Clamp values to safe boundaries defined in config.h
    if (tdsValue < TDS_MIN) tdsValue = TDS_MIN;
    if (tdsValue > TDS_MAX) tdsValue = TDS_MAX;
    
    // --- Moving Average Filter (Alpha 0.1) ---
    if (_tdsMovingAverage < 0) {
        _tdsMovingAverage = tdsValue;
    } else {
        _tdsMovingAverage = (tdsValue * 0.1f) + (_tdsMovingAverage * 0.9f);
    }
    
    TDS_LOCK();
    _tdsLastResult = _tdsMovingAverage;
    TDS_UNLOCK();
    return _tdsMovingAverage;
}

float tdsGetLastValue(void) {
    float lastValue = -1.0f;
    TDS_LOCK();
    lastValue = _tdsLastResult;
    TDS_UNLOCK();
    return lastValue;
}

void tdsSetCalibration(float lowPpm, float lowVoltage, float highPpm, float highVoltage) {
    _calLowPpm = lowPpm;
    _calLowVoltage = lowVoltage;
    _calHighPpm = highPpm;
    _calHighVoltage = highVoltage;
    
    if (highVoltage != lowVoltage) {
        _calculateCalibrationFactors(); // Calculate K & Offset
        _isCalibrated = true;
        _saveCalibrationToNVS();
        
        LOG_INFO("TDS Calibration set (Hybrid Mode):");
        LOG_INFO("  K: %.4f, Offset: %.4f", _kValue, _offset);
    } else {
        LOG_ERROR("TDS Calibration failed: voltages are the same!");
        _isCalibrated = false;
    }
}

void tdsLoop(float temperature) {
    // ตรวจสอบเวลา (Non-blocking delay)
    if (millis() - _tdsLastReadTime >= TDS_READ_INTERVAL) {
        _tdsLastReadTime = millis();
        tdsRead(temperature);
    }
}

