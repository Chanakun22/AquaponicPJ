/**
 * @file phSensor.cpp
 * @brief Implementation สำหรับ pH Sensor
 * @details รองรับ NVS storage และ Temperature Compensation
 */

#include "phSensor.h"
#include <Preferences.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static Preferences _prefs;                    // NVS storage
static float _lastPh = -1.0;
static bool _sensorReady = false;
static unsigned long _lastReadTime = 0;
static int _sampleBuffer[PH_SAMPLE_COUNT];
static int _sampleIndex = 0;
static bool _bufferFull = false;

// Calibration values (stored in NVS)
static int _calibVoltage7 = PH_VOLTAGE_AT_7;  // ADC value at pH 7.0
static int _calibVoltage4 = 0;                 // ADC value at pH 4.0
static float _calibSlope = 0;                  // Calculated slope (mV/pH)

// Temperature for compensation
static float _waterTemperature = 25.0;         // Default 25°C

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief เรียงลำดับ array (Bubble Sort)
 */
static void _sortArray(int arr[], int size) {
    for (int i = 0; i < size - 1; i++) {
        for (int j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

/**
 * @brief คำนวณค่าเฉลี่ยแบบตัด outliers (Median Filter)
 */
static int _getMedianValue(void) {
    int tempBuffer[PH_SAMPLE_COUNT];
    memcpy(tempBuffer, _sampleBuffer, sizeof(_sampleBuffer));
    _sortArray(tempBuffer, PH_SAMPLE_COUNT);
    
    // ตัด 20% บนและล่าง แล้วหาค่าเฉลี่ย
    int trimCount = PH_SAMPLE_COUNT / 5;
    long sum = 0;
    int count = 0;
    
    for (int i = trimCount; i < PH_SAMPLE_COUNT - trimCount; i++) {
        sum += tempBuffer[i];
        count++;
    }
    
    return count > 0 ? (sum / count) : tempBuffer[PH_SAMPLE_COUNT / 2];
}

/**
 * @brief คำนวณ slope ตาม Nernst Equation พร้อม Temperature Compensation
 * @param temperature อุณหภูมิน้ำ (°C)
 * @return slope (mV/pH)
 */
static float _getTemperatureCompensatedSlope(float temperature) {
    // Nernst Equation: slope = -2.303 * R * T / (n * F)
    // At 25°C (298.15K): slope = -59.16 mV/pH
    // At T°C: slope = -59.16 * (T + 273.15) / 298.15
    
    float baseSlope = (_calibSlope != 0) ? _calibSlope : PH_VOLTAGE_SLOPE;
    
    // ถ้าใช้ calibrated slope ให้ปรับตามอุณหภูมิ
    // (calibrated slope คือค่าที่ได้ที่อุณหภูมิ calibration ~25°C)
    float compensatedSlope = baseSlope * (temperature + 273.15) / 298.15;
    
    return compensatedSlope;
}

/**
 * @brief แปลง ADC value เป็น pH พร้อม Temperature Compensation
 */
static float _voltageToPhNeutral(int adcValue) {
    float voltage = (adcValue / 4095.0) * 3300.0;  // Convert to mV
    float voltageAt7 = (_calibVoltage7 / 4095.0) * 3300.0;
    
    // Get temperature-compensated slope
    float slope = _getTemperatureCompensatedSlope(_waterTemperature);
    
    float ph = 7.0 + ((voltage - voltageAt7) / slope);
    
    // Clamp to valid pH range
    if (ph < 0) ph = 0;
    if (ph > 14) ph = 14;
    
    return ph;
}

/**
 * @brief บันทึกค่า Calibration ลง NVS
 */
static void _saveCalibrationToNVS(void) {
    _prefs.putInt("volt7", _calibVoltage7);
    _prefs.putInt("volt4", _calibVoltage4);
    _prefs.putFloat("slope", _calibSlope);
    Serial.println(F("[PH] Calibration saved to NVS!"));
}

/**
 * @brief โหลดค่า Calibration จาก NVS
 */
static void _loadCalibrationFromNVS(void) {
    _calibVoltage7 = _prefs.getInt("volt7", PH_VOLTAGE_AT_7);
    _calibVoltage4 = _prefs.getInt("volt4", 0);
    _calibSlope = _prefs.getFloat("slope", 0);
    
    Serial.println(F("[PH] Loaded calibration from NVS:"));
    Serial.printf("[PH]   Voltage@pH7: %d (%.1f mV)\n", _calibVoltage7, (_calibVoltage7 / 4095.0) * 3300.0);
    if (_calibVoltage4 != 0) {
        Serial.printf("[PH]   Voltage@pH4: %d (%.1f mV)\n", _calibVoltage4, (_calibVoltage4 / 4095.0) * 3300.0);
        Serial.printf("[PH]   Slope: %.2f mV/pH\n", _calibSlope);
    } else {
        Serial.println(F("[PH]   pH 4.0 not calibrated yet"));
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void phSetup(void) {
    Serial.println(F("[PH] Initializing pH Sensor..."));
    
    // เปิด NVS namespace
    _prefs.begin("phSensor", false);
    
    // โหลดค่า Calibration จาก NVS
    _loadCalibrationFromNVS();
    
    // ตั้งค่า ADC
    pinMode(PH_SENSOR_PIN, INPUT);
    analogSetAttenuation(ADC_11db);  // Full range 0-3.3V
    
    // Reset buffer
    memset(_sampleBuffer, 0, sizeof(_sampleBuffer));
    _sampleIndex = 0;
    _bufferFull = false;
    _sensorReady = false;
    
    Serial.print(F("[PH] PIN: "));
    Serial.println(PH_SENSOR_PIN);
    Serial.println(F("[PH] Warming up... (30 samples needed)"));
}

void phLoop(void) {
    unsigned long currentTime = millis();
    
    // อ่านค่าตาม interval
    if (currentTime - _lastReadTime >= PH_READ_INTERVAL) {
        _lastReadTime = currentTime;
        
        // อ่านค่า ADC
        int rawValue = analogRead(PH_SENSOR_PIN);
        
        // เก็บลง buffer
        _sampleBuffer[_sampleIndex] = rawValue;
        _sampleIndex = (_sampleIndex + 1) % PH_SAMPLE_COUNT;
        
        if (_sampleIndex == 0) {
            _bufferFull = true;
        }
        
        // คำนวณ pH เมื่อ buffer เต็ม
        if (_bufferFull) {
            if (!_sensorReady) {
                _sensorReady = true;
                Serial.println(F("[PH] Sensor ready!"));
            }
            
            int medianValue = _getMedianValue();
            _lastPh = _voltageToPhNeutral(medianValue);
        }
    }
}

float phRead(void) {
    return _sensorReady ? _lastPh : -1.0;
}

float phReadVoltage(void) {
    if (!_bufferFull) return -1.0;
    int medianValue = _getMedianValue();
    return (medianValue / 4095.0) * 3300.0;  // mV
}

bool phIsReady(void) {
    return _sensorReady;
}

void phSetTemperature(float temperature) {
    // ตรวจสอบค่าที่ valid
    if (temperature >= 0 && temperature <= 100) {
        _waterTemperature = temperature;
    }
}

void phCalibratePh7(void) {
    if (!_bufferFull) {
        Serial.println(F("[PH] ERROR: Need more samples. Wait for sensor ready."));
        return;
    }
    
    _calibVoltage7 = _getMedianValue();
    float voltage = (_calibVoltage7 / 4095.0) * 3300.0;
    
    Serial.println(F("[PH] ===== CALIBRATION pH 7.0 ====="));
    Serial.printf("[PH] ADC Value: %d\n", _calibVoltage7);
    Serial.printf("[PH] Voltage: %.1f mV\n", voltage);
    Serial.printf("[PH] Temperature: %.1f °C\n", _waterTemperature);
    
    // Recalculate slope if pH 4 calibration exists
    if (_calibVoltage4 != 0) {
        float voltage4 = (_calibVoltage4 / 4095.0) * 3300.0;
        float voltage7 = voltage;
        _calibSlope = (voltage4 - voltage7) / (4.0 - 7.0);
        Serial.printf("[PH] Calculated slope: %.2f mV/pH\n", _calibSlope);
    }
    
    // บันทึกลง NVS
    _saveCalibrationToNVS();
    Serial.println(F("[PH] ================================"));
}

void phCalibratePh4(void) {
    if (!_bufferFull) {
        Serial.println(F("[PH] ERROR: Need more samples. Wait for sensor ready."));
        return;
    }
    
    _calibVoltage4 = _getMedianValue();
    float voltage = (_calibVoltage4 / 4095.0) * 3300.0;
    
    Serial.println(F("[PH] ===== CALIBRATION pH 4.0 ====="));
    Serial.printf("[PH] ADC Value: %d\n", _calibVoltage4);
    Serial.printf("[PH] Voltage: %.1f mV\n", voltage);
    Serial.printf("[PH] Temperature: %.1f °C\n", _waterTemperature);
    
    // Calculate slope if pH 7 calibration exists
    if (_calibVoltage7 != 0) {
        float voltage4 = voltage;
        float voltage7 = (_calibVoltage7 / 4095.0) * 3300.0;
        _calibSlope = (voltage4 - voltage7) / (4.0 - 7.0);
        Serial.printf("[PH] Calculated slope: %.2f mV/pH\n", _calibSlope);
    }
    
    // บันทึกลง NVS
    _saveCalibrationToNVS();
    Serial.println(F("[PH] ================================"));
}

void phClearCalibration(void) {
    _prefs.clear();
    _calibVoltage7 = PH_VOLTAGE_AT_7;
    _calibVoltage4 = 0;
    _calibSlope = 0;
    Serial.println(F("[PH] Calibration cleared from NVS!"));
}

