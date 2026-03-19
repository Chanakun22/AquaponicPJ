/**
 * @file phSensor.cpp
 * @brief Implementation สำหรับ pH Sensor
 * @details รองรับ NVS storage และ Temperature Compensation
 */

#include "phSensor.h"
#include "config.h"
#include "logger.h"
#include <Preferences.h>


// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static Preferences _prefs; // NVS storage
static float _lastPh = -1.0;
static bool _sensorReady = false;
static unsigned long _lastReadTime = 0;
static int _sampleBuffer[PH_SAMPLE_COUNT];
static int _sampleIndex = 0;
static bool _bufferFull = false;

// Calibration values (stored in NVS)
static int _calibVoltage7 = PH_VOLTAGE_AT_7; // ADC value at pH 7.0
static int _calibVoltage4 = 0;               // ADC value at pH 4.0
static float _calibSlope = 0;                // Calculated slope (mV/pH)

// Temperature for compensation
static float _waterTemperature = 25.0; // Default 25°C

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
  // Hardware-in-the-Loop Validation: Detect wire disconnects (0V) or shorts
  // (3.3V)
  if (adcValue < 10 || adcValue > 4085) {
    return NAN;
  }

  float voltage = (adcValue / 4095.0) * 3300.0; // Convert to mV
  float voltageAt7 = (_calibVoltage7 / 4095.0) * 3300.0;

  // Get temperature-compensated slope
  float slope = _getTemperatureCompensatedSlope(_waterTemperature);

  float ph = 7.0 + ((voltage - voltageAt7) / slope);

  // Clamp to valid pH range from config.h
  if (ph < PH_MIN)
    ph = PH_MIN;
  if (ph > PH_MAX)
    ph = PH_MAX;

  return ph;
}

/**
 * @brief บันทึกค่า Calibration ลง NVS
 */
static void _saveCalibrationToNVS(void) {
  _prefs.begin("phSensor", false); // Open read-write
  _prefs.putInt("volt7", _calibVoltage7);
  _prefs.putInt("volt4", _calibVoltage4);
  _prefs.putFloat("slope", _calibSlope);
  _prefs.end(); // Close to flush data to flash
  LOG_INFO("pH calibration saved to NVS");
}

/**
 * @brief โหลดค่า Calibration จาก NVS
 */
static void _loadCalibrationFromNVS(void) {
  _prefs.begin("phSensor", true); // Open read-only
  _calibVoltage7 = _prefs.getInt("volt7", PH_VOLTAGE_AT_7);
  _calibVoltage4 = _prefs.getInt("volt4", 0);
  _calibSlope = _prefs.getFloat("slope", 0);
  _prefs.end(); // Close after reading

  LOG_INFO("Loaded pH calibration from NVS:");
  LOG_INFO("  Voltage@pH7: %d (%.1f mV)", _calibVoltage7,
           (_calibVoltage7 / 4095.0) * 3300.0);
  if (_calibVoltage4 != 0) {
    LOG_INFO("  Voltage@pH4: %d (%.1f mV)", _calibVoltage4,
             (_calibVoltage4 / 4095.0) * 3300.0);
    LOG_INFO("  Slope: %.2f mV/pH", _calibSlope);
  } else {
    LOG_INFO("  pH 4.0 not calibrated yet");
  }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void phSetup(void) {
  Serial.println(F("[PH] Initializing pH Sensor..."));

  // โหลดค่า Calibration จาก NVS
  _loadCalibrationFromNVS();

  // ตั้งค่า ADC
  pinMode(PH_SENSOR_PIN, INPUT);
  analogSetAttenuation(ADC_11db); // Full range 0-3.3V

  // Reset buffer
  memset(_sampleBuffer, 0, sizeof(_sampleBuffer));
  _sampleIndex = 0;
  _bufferFull = false;
  _sensorReady = false;

  LOG_INFO("pH sensor PIN: %d", PH_SENSOR_PIN);
  LOG_INFO("Warming up... (%d samples needed)", PH_SAMPLE_COUNT);
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
        LOG_INFO("pH sensor ready!");
      }

      int medianValue = _getMedianValue();
      float rawPh = _voltageToPhNeutral(medianValue);

      // EMA Filter (Alpha 0.15) for smooth display across cycles
      static float _phMovingAverage = -1.0f;
      if (isnan(rawPh)) {
        _lastPh = NAN;
      } else if (_phMovingAverage < 0) {
        _phMovingAverage = rawPh;
        _lastPh = rawPh;
      } else {
        _phMovingAverage = (rawPh * 0.15f) + (_phMovingAverage * 0.85f);
        _lastPh = _phMovingAverage;
      }
    }
  }
}

float phRead(void) { return _sensorReady ? _lastPh : -1.0; }

float phReadVoltage(void) {
  if (!_bufferFull)
    return -1.0;
  int medianValue = _getMedianValue();
  return (medianValue / 4095.0) * 3300.0; // mV
}

bool phIsReady(void) { return _sensorReady; }

void phSetTemperature(float temperature) {
  // ตรวจสอบค่าที่ valid
  if (temperature >= 0 && temperature <= 100) {
    _waterTemperature = temperature;
  }
}

void phCalibratePh7(void) {
  if (!_bufferFull) {
    LOG_ERROR("Need more samples. Wait for sensor ready.");
    return;
  }

  _calibVoltage7 = _getMedianValue();
  float voltage = (_calibVoltage7 / 4095.0) * 3300.0;

  LOG_INFO("===== CALIBRATION pH 7.0 =====");
  LOG_INFO("ADC Value: %d", _calibVoltage7);
  LOG_INFO("Voltage: %.1f mV", voltage);
  LOG_INFO("Temperature: %.1f °C", _waterTemperature);

  // Recalculate slope if pH 4 calibration exists
  if (_calibVoltage4 != 0) {
    float voltage4 = (_calibVoltage4 / 4095.0) * 3300.0;
    float voltage7 = voltage;
    _calibSlope = (voltage4 - voltage7) / (4.0 - 7.0);
    LOG_INFO("Calculated slope: %.2f mV/pH", _calibSlope);
  }

  // บันทึกลง NVS
  _saveCalibrationToNVS();
  LOG_INFO("================================");
}

void phCalibratePh4(void) {
  if (!_bufferFull) {
    LOG_ERROR("Need more samples. Wait for sensor ready.");
    return;
  }

  _calibVoltage4 = _getMedianValue();
  float voltage = (_calibVoltage4 / 4095.0) * 3300.0;

  LOG_INFO("===== CALIBRATION pH 4.0 =====");
  LOG_INFO("ADC Value: %d", _calibVoltage4);
  LOG_INFO("Voltage: %.1f mV", voltage);
  LOG_INFO("Temperature: %.1f °C", _waterTemperature);

  // Calculate slope if pH 7 calibration exists
  if (_calibVoltage7 != 0) {
    float voltage4 = voltage;
    float voltage7 = (_calibVoltage7 / 4095.0) * 3300.0;
    _calibSlope = (voltage4 - voltage7) / (4.0 - 7.0);
    LOG_INFO("Calculated slope: %.2f mV/pH", _calibSlope);
  }

  // บันทึกลง NVS
  _saveCalibrationToNVS();
  LOG_INFO("================================");
}

void phClearCalibration(void) {
  _prefs.begin("phSensor", false); // Open read-write
  _prefs.clear();
  _prefs.end(); // Close to flush

  _calibVoltage7 = PH_VOLTAGE_AT_7;
  _calibVoltage4 = 0;
  _calibSlope = 0;
  LOG_INFO("pH calibration cleared from NVS");
}
