/**
 * @file TdsSensor.cpp
 * @brief Implementation สำหรับ TDS Sensor พร้อม 2-Point Calibration + NVS Storage
 */

#include "TdsSensor.h"
#include "logger.h"
#include <Preferences.h>

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
    
    LOG_INFO("TDS sensor initialized, collecting %d samples...", TDS_SAMPLE_COUNT);
    
    if (_isCalibrated) {
        LOG_INFO("TDS Calibration loaded from NVS: Low=%.0f ppm @ %.3fV, High=%.0f ppm @ %.3fV", 
                 _calLowPpm, _calLowVoltage, _calHighPpm, _calHighVoltage);
    } else {
        LOG_WARN("TDS not calibrated, using default formula");
    }
}

float tdsGetVoltage(void) {
    if (!_tdsReady) return -1.0f;
    
    // คัดลอกไปใส่ตัวแปรชั่วคราวเพื่อคำนวณ
    int tempBuffer[TDS_SAMPLE_COUNT];
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
        tempBuffer[i] = _tdsBuffer[i];
    }
    
    // แปลงค่า ADC Median เป็น Voltage
    float rawVoltage = _getMedian(tempBuffer, TDS_SAMPLE_COUNT) * TDS_VREF / TDS_ADC_RESOLUTION;
    
    // Moving Average Filter for smooth display (Alpha 0.15)
    static float _voltageMovingAverage = -1.0f;
    if (_voltageMovingAverage < 0) {
        _voltageMovingAverage = rawVoltage;
    } else {
        _voltageMovingAverage = (rawVoltage * 0.15f) + (_voltageMovingAverage * 0.85f);
    }
    
    return _voltageMovingAverage;
}

bool tdsIsReady(void) {
    return _tdsReady;
}

bool tdsIsCalibrated(void) {
    return _isCalibrated;
}



float tdsRead(float temperature) {
    _tdsBuffer[_tdsBufferIndex] = analogRead(TDS_PIN);
    _tdsBufferIndex = (_tdsBufferIndex + 1) % TDS_SAMPLE_COUNT;
    
    if (!_tdsReady && _tdsBufferIndex == 0) {
        _tdsReady = true;
        LOG_INFO("TDS buffer full, sensors ready!");
    }
    
    if (!_tdsReady) return -1.0f;
    
    float voltage = tdsGetVoltage();
    
    // --- Advanced Temperature Compensation ---
    // Standard linear factor is 1/(1+0.02(T-25))
    // Optimized for nutrient solution (approx 0.019 coeff)
    if (isnan(temperature)) {
        _tdsLastResult = NAN;
        return NAN;
    }
    float tempP = temperature;
    
    float compensationCoefficient = 1.0f + 0.019f * (tempP - 25.0f);
    float compensationVoltage = voltage / compensationCoefficient;
    
    float tdsValue = _calculateTdsFromVoltage(compensationVoltage);
    if (tdsValue < 0) tdsValue = 0;
    
    // --- Moving Average Filter (Alpha 0.1) ---
    static float _tdsMovingAverage = -1.0f;
    if (_tdsMovingAverage < 0) {
        _tdsMovingAverage = tdsValue;
    } else {
        _tdsMovingAverage = (tdsValue * 0.1f) + (_tdsMovingAverage * 0.9f);
    }
    
    _tdsLastResult = _tdsMovingAverage;
    return _tdsMovingAverage;
}

float tdsGetLastValue(void) {
    return _tdsLastResult;
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

