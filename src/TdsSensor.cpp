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
static float _calLowTemperature = 25.0f;
static float _calHighPpm = 0.0f;
static float _calHighVoltage = 0.0f;
static float _calHighTemperature = 25.0f;
static bool _calibrationUsesRawVoltage = false;

// Standard DFRobot polynomial returns EC@25C-like base value before applying
// the project-specific TDS conversion factor.
static float _getStandardEc(float voltage) {
    if (voltage <= 0) return 0;
    return (133.42f * voltage * voltage * voltage 
          - 255.86f * voltage * voltage 
          + 857.39f * voltage);
}

static float _getStandardTds(float voltage) {
    return _getStandardEc(voltage) * TDS_CONVERSION_FACTOR;
}

static float _getCompensatedVoltage(float voltage, float temperature) {
    if (!isfinite(voltage) || voltage <= 0.0f) {
        return 0.0f;
    }

    float safeTemperature = isfinite(temperature) ? temperature : 25.0f;
    float compensationCoefficient = 1.0f + 0.019f * (safeTemperature - 25.0f);
    if (compensationCoefficient <= 0.0f) {
        compensationCoefficient = 1.0f;
    }

    return voltage / compensationCoefficient;
}

static float _normalizeCalibrationVoltage(float voltage,
                                          float temperature,
                                          bool rawVoltageInput) {
    return rawVoltageInput ? _getCompensatedVoltage(voltage, temperature) : voltage;
}

static bool _hasValidCalibrationSpan(float lowVoltage,
                                     float lowTemperature,
                                     float highVoltage,
                                     float highTemperature,
                                     bool rawVoltageInput) {
    float normalizedLowVoltage = _normalizeCalibrationVoltage(lowVoltage, lowTemperature,
                                                              rawVoltageInput);
    float normalizedHighVoltage = _normalizeCalibrationVoltage(highVoltage,
                                                               highTemperature,
                                                               rawVoltageInput);
    return fabsf(normalizedHighVoltage - normalizedLowVoltage) >= TDS_MIN_CALIBRATION_SPAN_V;
}

static float _applyDeadband(float previousValue, float candidateValue, float deadband) {
    if (previousValue < 0.0f || isnan(previousValue)) {
        return candidateValue;
    }

    return fabsf(candidateValue - previousValue) < deadband ? previousValue : candidateValue;
}

static float _limitStep(float previousValue, float candidateValue, float maxStep) {
    if (previousValue < 0.0f || isnan(previousValue)) {
        return candidateValue;
    }

    float delta = candidateValue - previousValue;
    if (delta > maxStep) {
        return previousValue + maxStep;
    }
    if (delta < -maxStep) {
        return previousValue - maxStep;
    }
    return candidateValue;
}

static int _readOversampledAdc(void) {
    long sum = 0;

    for (int i = 0; i < TDS_ADC_DUMMY_READS; i++) {
        delayMicroseconds(TDS_ADC_SETTLE_US);
        (void)analogRead(TDS_PIN);
    }

    for (int i = 0; i < TDS_OVERSAMPLE_COUNT; i++) {
        delayMicroseconds(TDS_ADC_SETTLE_US);
        sum += analogRead(TDS_PIN);
    }

    return static_cast<int>(sum / TDS_OVERSAMPLE_COUNT);
}

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

static void _calculateCalibrationFactors() {
    float normalizedLowVoltage = _normalizeCalibrationVoltage(_calLowVoltage,
                                                              _calLowTemperature,
                                                              _calibrationUsesRawVoltage);
    float normalizedHighVoltage = _normalizeCalibrationVoltage(_calHighVoltage,
                                                               _calHighTemperature,
                                                               _calibrationUsesRawVoltage);

    if (!_hasValidCalibrationSpan(_calLowVoltage,
                                  _calLowTemperature,
                                  _calHighVoltage,
                                  _calHighTemperature,
                                  _calibrationUsesRawVoltage)) {
        _kValue = 1.0f;
        _offset = 0.0f;
        _isCalibrated = false;
        LOG_ERROR("TDS calibration rejected: voltage span %.3fV is too small (need >= %.3fV)",
                  fabsf(normalizedHighVoltage - normalizedLowVoltage),
                  TDS_MIN_CALIBRATION_SPAN_V);
        return;
    }
    
    // Normalize both calibration points back to the 25C reference used by the runtime formula.
    float baseLow = _getStandardTds(normalizedLowVoltage);
    float baseHigh = _getStandardTds(normalizedHighVoltage);
    if (fabsf(baseHigh - baseLow) < 0.0001f) {
        _kValue = 1.0f;
        _offset = 0.0f;
        return;
    }
    
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
        _calLowTemperature = _tdsPrefs.getFloat("lowT", 25.0f);
        _calHighPpm = _tdsPrefs.getFloat("highPpm", 0);
        _calHighVoltage = _tdsPrefs.getFloat("highV", 0);
        _calHighTemperature = _tdsPrefs.getFloat("highT", 25.0f);
        _calibrationUsesRawVoltage = _tdsPrefs.getBool("rawV", false);
        _calculateCalibrationFactors(); // Recalculate K/Offset
    }
    _tdsPrefs.end();
}

static void _saveCalibrationToNVS() {
    _tdsPrefs.begin("tdsSensor", false);
    _tdsPrefs.putBool("calibrated", _isCalibrated);
    _tdsPrefs.putFloat("lowPpm", _calLowPpm);
    _tdsPrefs.putFloat("lowV", _calLowVoltage);
    _tdsPrefs.putFloat("lowT", _calLowTemperature);
    _tdsPrefs.putFloat("highPpm", _calHighPpm);
    _tdsPrefs.putFloat("highV", _calHighVoltage);
    _tdsPrefs.putFloat("highT", _calHighTemperature);
    _tdsPrefs.putBool("rawV", _calibrationUsesRawVoltage);
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
    // 1. Get Base TDS from standard EC polynomial + configurable conversion factor
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
    float previousVoltage = -1.0f;

    TDS_LOCK();
    ready = _tdsReady;
    previousVoltage = _tdsLastVoltage;
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
        tempBuffer[i] = _tdsBuffer[i];
    }
    TDS_UNLOCK();

    if (!ready) {
        return -1.0f;
    }

    float rawVoltage = _getMedian(tempBuffer, TDS_SAMPLE_COUNT) * TDS_VREF / TDS_ADC_RESOLUTION;

    if (_tdsVoltageMovingAverage < 0 || isnan(_tdsVoltageMovingAverage)) {
        _tdsVoltageMovingAverage = rawVoltage;
    } else {
        _tdsVoltageMovingAverage =
            (rawVoltage * TDS_VOLTAGE_FILTER_ALPHA) +
            (_tdsVoltageMovingAverage * (1.0f - TDS_VOLTAGE_FILTER_ALPHA));
    }

    return _applyDeadband(previousVoltage, _tdsVoltageMovingAverage, TDS_VOLTAGE_DEADBAND_V);
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void tdsSetup(void) {
    pinMode(TDS_PIN, INPUT);

#if defined(ESP32)
    analogSetAttenuation(ADC_11db);
#endif
    
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
        LOG_INFO("TDS Calibration loaded from NVS: Low=%.0f ppm @ %.3fV / %.1fC, High=%.0f ppm @ %.3fV / %.1fC, mode=%s", 
             _calLowPpm, _calLowVoltage, _calLowTemperature, _calHighPpm, _calHighVoltage, _calHighTemperature,
             _calibrationUsesRawVoltage ? "raw" : "compensated");
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
    int sample = _readOversampledAdc();
    bool becameReady = false;
    float previousTds = -1.0f;

    TDS_LOCK();
    previousTds = _tdsLastResult;
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
    float compensationVoltage = _getCompensatedVoltage(voltage, temperature);
    
    float tdsValue = _calculateTdsFromVoltage(compensationVoltage);
    
    // Clamp values to safe boundaries defined in config.h
    if (tdsValue < TDS_MIN) tdsValue = TDS_MIN;
    if (tdsValue > TDS_MAX) tdsValue = TDS_MAX;
    
    // --- Stabilization Layer ---
    if (_tdsMovingAverage < 0 || isnan(_tdsMovingAverage)) {
        _tdsMovingAverage = tdsValue;
    } else {
        _tdsMovingAverage =
            (tdsValue * TDS_VALUE_FILTER_ALPHA) +
            (_tdsMovingAverage * (1.0f - TDS_VALUE_FILTER_ALPHA));
        float stabilizedTds = _applyDeadband(previousTds, _tdsMovingAverage,
                                             TDS_VALUE_DEADBAND_PPM);
        _tdsMovingAverage = _limitStep(previousTds, stabilizedTds,
                                       TDS_VALUE_MAX_STEP_PPM);
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

void tdsSetCalibration(float lowPpm,
                       float lowVoltage,
                       float lowTemperature,
                       float highPpm,
                       float highVoltage,
                       float highTemperature,
                       bool rawVoltageInput) {
    _calLowPpm = lowPpm;
    _calLowVoltage = lowVoltage;
    _calLowTemperature = isfinite(lowTemperature) ? lowTemperature : 25.0f;
    _calHighPpm = highPpm;
    _calHighVoltage = highVoltage;
    _calHighTemperature = isfinite(highTemperature) ? highTemperature : 25.0f;
    _calibrationUsesRawVoltage = rawVoltageInput;
    
    if (_hasValidCalibrationSpan(_calLowVoltage,
                                 _calLowTemperature,
                                 _calHighVoltage,
                                 _calHighTemperature,
                                 _calibrationUsesRawVoltage)) {
        _calculateCalibrationFactors(); // Calculate K & Offset
        _isCalibrated = true;
        _saveCalibrationToNVS();
        
        // Reset EMA state so readings converge immediately to new calibration
        _tdsMovingAverage = -1.0f;
        _tdsVoltageMovingAverage = -1.0f;
        
        LOG_INFO("TDS Calibration set (Hybrid Mode):");
        LOG_INFO("  K: %.4f, Offset: %.4f, LowTemp: %.1fC, HighTemp: %.1fC, mode=%s", _kValue, _offset, _calLowTemperature, _calHighTemperature, _calibrationUsesRawVoltage ? "raw" : "compensated");
    } else {
        float normalizedLowVoltage = _normalizeCalibrationVoltage(_calLowVoltage,
                                                                  _calLowTemperature,
                                                                  _calibrationUsesRawVoltage);
        float normalizedHighVoltage = _normalizeCalibrationVoltage(_calHighVoltage,
                                                                   _calHighTemperature,
                                                                   _calibrationUsesRawVoltage);
        LOG_ERROR("TDS Calibration failed: voltage span %.3fV is too small (need >= %.3fV)",
                  fabsf(normalizedHighVoltage - normalizedLowVoltage),
                  TDS_MIN_CALIBRATION_SPAN_V);
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

