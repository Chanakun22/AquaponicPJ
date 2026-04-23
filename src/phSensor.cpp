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
static float _lastVoltageMv = -1.0;
static float _displayVoltageMv = -1.0;
static bool _sensorReady = false;
static unsigned long _lastReadTime = 0;
static int _sampleBuffer[PH_SAMPLE_COUNT];
static int _sampleIndex = 0;
static bool _bufferFull = false;

// Calibration values (stored in NVS)
static int _calibVoltage401 = 0;
static int _calibVoltage686 = PH_VOLTAGE_AT_686;
static int _calibVoltage918 = 0;
static bool _hasCalib401 = false;
static bool _hasCalib686 = false;
static bool _hasCalib918 = false;
static uint8_t _invalidReadStreak = 0;

// Temperature for compensation
static float _waterTemperature = 25.0; // Default 25°C

struct PhCalibrationPoint {
  float ph;
  float voltage;
};

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

static float _adcToMillivolts(int adcValue) {
  return (adcValue / 4095.0f) * 3300.0f;
}

static bool _isAdcReadingValid(int adcValue) {
  return adcValue >= 10 && adcValue <= 4085;
}

static float _applyDeadband(float previousValue, float candidateValue,
                            float deadband) {
  if (previousValue < 0 || isnan(previousValue)) {
    return candidateValue;
  }

  return fabsf(candidateValue - previousValue) < deadband ? previousValue
                                                          : candidateValue;
}

static float _limitStep(float previousValue, float candidateValue,
                        float maxStep) {
  if (previousValue < 0 || isnan(previousValue)) {
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

  for (int i = 0; i < PH_ADC_DUMMY_READS; i++) {
    delayMicroseconds(PH_ADC_SETTLE_US);
    (void)analogRead(PH_SENSOR_PIN);
  }

  for (int i = 0; i < PH_OVERSAMPLE_COUNT; i++) {
    delayMicroseconds(PH_ADC_SETTLE_US);
    sum += analogRead(PH_SENSOR_PIN);
  }

  return static_cast<int>(sum / PH_OVERSAMPLE_COUNT);
}

static void _sortCalibrationPoints(PhCalibrationPoint points[], int count) {
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (points[j].ph > points[j + 1].ph) {
        PhCalibrationPoint temp = points[j];
        points[j] = points[j + 1];
        points[j + 1] = temp;
      }
    }
  }
}

static float _compensateSlope(float slope, float temperature) {
  return slope * (temperature + 273.15f) / 298.15f;
}

static float _calculatePhFromSegment(float voltage, float referenceVoltage,
                                     float referencePh, float slope) {
  float compensatedSlope = _compensateSlope(slope, _waterTemperature);
  return referencePh + ((voltage - referenceVoltage) / compensatedSlope);
}

static int _collectCalibrationPoints(PhCalibrationPoint points[]) {
  int count = 0;

  if (_hasCalib401 && _calibVoltage401 > 0) {
    points[count++] = {PH_CAL_POINT_401, _adcToMillivolts(_calibVoltage401)};
  }

  points[count++] = {PH_CAL_POINT_686, _adcToMillivolts(_calibVoltage686)};

  if (_hasCalib918 && _calibVoltage918 > 0) {
    points[count++] = {PH_CAL_POINT_918, _adcToMillivolts(_calibVoltage918)};
  }

  _sortCalibrationPoints(points, count);
  return count;
}

static float _calculateSlope(float voltage1, float ph1, float voltage2,
                             float ph2) {
  if (ph1 == ph2) {
    return PH_VOLTAGE_SLOPE;
  }
  return (voltage2 - voltage1) / (ph2 - ph1);
}

static void _logCalibrationState(void) {
  LOG_INFO("Loaded pH calibration from NVS:");
  if (_hasCalib401) {
    LOG_INFO("  Voltage@pH%.2f: %d (%.1f mV)", PH_CAL_POINT_401,
             _calibVoltage401, _adcToMillivolts(_calibVoltage401));
  } else {
    LOG_INFO("  pH %.2f not calibrated yet", PH_CAL_POINT_401);
  }

  LOG_INFO("  Voltage@pH%.2f: %d (%.1f mV)%s", PH_CAL_POINT_686,
           _calibVoltage686, _adcToMillivolts(_calibVoltage686),
           _hasCalib686 ? "" : " [default]");

  if (_hasCalib918) {
    LOG_INFO("  Voltage@pH%.2f: %d (%.1f mV)", PH_CAL_POINT_918,
             _calibVoltage918, _adcToMillivolts(_calibVoltage918));
  } else {
    LOG_INFO("  pH %.2f not calibrated yet", PH_CAL_POINT_918);
  }
}

/**
 * @brief คำนวณ slope ตาม Nernst Equation พร้อม Temperature Compensation
 * @param temperature อุณหภูมิน้ำ (°C)
 * @return slope (mV/pH)
 */
static float _getTemperatureCompensatedSlope(float temperature) {
  return _compensateSlope(PH_VOLTAGE_SLOPE, temperature);
}

/**
 * @brief แปลงแรงดันเป็น pH พร้อม Temperature Compensation
 */
static float _millivoltsToPh(float voltage) {
  PhCalibrationPoint points[3];
  int pointCount = _collectCalibrationPoints(points);
  float ph = NAN;

  if (pointCount >= 3) {
    bool useLowSegment = (voltage >= points[1].voltage && voltage <= points[0].voltage) ||
                         (voltage <= points[1].voltage && voltage >= points[0].voltage) ||
                         (voltage > points[0].voltage);
    int startIndex = useLowSegment ? 0 : 1;
    float slope = _calculateSlope(points[startIndex].voltage, points[startIndex].ph,
                                  points[startIndex + 1].voltage, points[startIndex + 1].ph);
    ph = _calculatePhFromSegment(voltage, points[startIndex].voltage,
                                 points[startIndex].ph, slope);
  } else if (pointCount == 2) {
    float slope = _calculateSlope(points[0].voltage, points[0].ph,
                                  points[1].voltage, points[1].ph);
    ph = _calculatePhFromSegment(voltage, points[0].voltage, points[0].ph, slope);
  } else {
    float referenceVoltage = _adcToMillivolts(_calibVoltage686);
    float slope = _getTemperatureCompensatedSlope(_waterTemperature);
    ph = PH_CAL_POINT_686 + ((voltage - referenceVoltage) / slope);
  }

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
  if (_hasCalib401 && _calibVoltage401 > 0) {
    _prefs.putInt("v401", _calibVoltage401);
  } else {
    _prefs.remove("v401");
  }

  if (_hasCalib686) {
    _prefs.putInt("v686", _calibVoltage686);
  } else {
    _prefs.remove("v686");
  }

  if (_hasCalib918 && _calibVoltage918 > 0) {
    _prefs.putInt("v918", _calibVoltage918);
  } else {
    _prefs.remove("v918");
  }

  _prefs.remove("volt7");
  _prefs.remove("volt4");
  _prefs.remove("slope");
  _prefs.end(); // Close to flush data to flash
  LOG_INFO("pH calibration saved to NVS");
}

/**
 * @brief โหลดค่า Calibration จาก NVS
 */
static void _loadCalibrationFromNVS(void) {
  _prefs.begin("phSensor", true); // Open read-only
  _hasCalib401 = _prefs.isKey("v401");
  _hasCalib686 = _prefs.isKey("v686");
  _hasCalib918 = _prefs.isKey("v918");

  _calibVoltage401 = _prefs.getInt("v401", 0);
  _calibVoltage686 = _prefs.getInt("v686", PH_VOLTAGE_AT_686);
  _calibVoltage918 = _prefs.getInt("v918", 0);

  bool hasLegacy4 = _prefs.isKey("volt4");
  bool hasLegacy7 = _prefs.isKey("volt7");
  int legacyVoltage4 = _prefs.getInt("volt4", 0);
  int legacyVoltage7 = _prefs.getInt("volt7", PH_VOLTAGE_AT_686);
  _prefs.end(); // Close after reading

  if (!_hasCalib401 && hasLegacy4 && legacyVoltage4 > 0) {
    _calibVoltage401 = legacyVoltage4;
    _hasCalib401 = true;
    LOG_WARN("Migrated legacy pH 4.0 calibration into pH 4.01 slot. Recalibrate with pH 4.01 buffer for best accuracy.");
  }

  if (!_hasCalib686 && hasLegacy7) {
    _calibVoltage686 = legacyVoltage7;
    _hasCalib686 = true;
    LOG_WARN("Migrated legacy pH 7.0 calibration into pH 6.86 slot. Recalibrate with pH 6.86 buffer for best accuracy.");
  }

  _logCalibrationState();
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
  _lastPh = -1.0f;
  _lastVoltageMv = -1.0f;
  _displayVoltageMv = -1.0;
  _invalidReadStreak = 0;

  LOG_INFO("pH sensor PIN: %d", PH_SENSOR_PIN);
  LOG_INFO("Warming up... (%d samples needed)", PH_SAMPLE_COUNT);
}

void phLoop(void) {
  unsigned long currentTime = millis();

  // อ่านค่าตาม interval
  if (currentTime - _lastReadTime >= PH_READ_INTERVAL) {
    _lastReadTime = currentTime;

    // อ่านค่า ADC หลายครั้งต่อรอบเพื่อลด noise จาก analog front-end
    int rawValue = _readOversampledAdc();

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
      static float _phMovingAverage = -1.0f;
      static float _voltageMovingAverage = -1.0f;
      static float _displayVoltageMovingAverage = -1.0f;

      if (!_isAdcReadingValid(medianValue)) {
        _invalidReadStreak++;
        if (_invalidReadStreak >= PH_INVALID_STREAK_LIMIT) {
          _lastPh = NAN;
          _lastVoltageMv = _adcToMillivolts(medianValue);
          _displayVoltageMv = _lastVoltageMv;
          _voltageMovingAverage = _lastVoltageMv;
          _displayVoltageMovingAverage = _lastVoltageMv;
        }
      } else {
        _invalidReadStreak = 0;
        float rawVoltage = _adcToMillivolts(medianValue);

        if (_voltageMovingAverage < 0 || isnan(_voltageMovingAverage)) {
          _voltageMovingAverage = rawVoltage;
        } else {
          _voltageMovingAverage =
              (rawVoltage * PH_VOLTAGE_FILTER_ALPHA) +
              (_voltageMovingAverage * (1.0f - PH_VOLTAGE_FILTER_ALPHA));
        }

        _lastVoltageMv = _applyDeadband(_lastVoltageMv, _voltageMovingAverage,
                                        PH_VOLTAGE_DEADBAND_MV);

        if (_displayVoltageMovingAverage < 0 || isnan(_displayVoltageMovingAverage)) {
          _displayVoltageMovingAverage = _lastVoltageMv;
        } else {
          _displayVoltageMovingAverage =
              (_lastVoltageMv * PH_VOLTAGE_DISPLAY_FILTER_ALPHA) +
              (_displayVoltageMovingAverage * (1.0f - PH_VOLTAGE_DISPLAY_FILTER_ALPHA));
        }

        _displayVoltageMv = _applyDeadband(_displayVoltageMv,
                                           _displayVoltageMovingAverage,
                                           PH_VOLTAGE_DISPLAY_DEADBAND_MV);

        float rawPh = _millivoltsToPh(_lastVoltageMv);
        if (isnan(rawPh)) {
          _lastPh = NAN;
        } else if (_phMovingAverage < 0 || isnan(_phMovingAverage)) {
          _phMovingAverage = rawPh;
          _lastPh = rawPh;
        } else {
          _phMovingAverage = (rawPh * PH_PH_FILTER_ALPHA) +
                             (_phMovingAverage * (1.0f - PH_PH_FILTER_ALPHA));
          float stabilizedPh = _applyDeadband(_lastPh, _phMovingAverage,
                                              PH_PH_DEADBAND);
          _lastPh = _limitStep(_lastPh, stabilizedPh, PH_PH_MAX_STEP);
          _phMovingAverage = _lastPh;
        }
      }
    }
  }
}

float phRead(void) { return _sensorReady ? _lastPh : -1.0; }

float phReadVoltage(void) {
  if (!_bufferFull)
    return -1.0;
  return _displayVoltageMv;
}

bool phIsReady(void) { return _sensorReady; }

void phSetTemperature(float temperature) {
  // ตรวจสอบค่าที่ valid
  if (temperature >= 0 && temperature <= 100) {
    _waterTemperature = temperature;
  }
}

static void _calibratePoint(float targetPh, int* storage, bool* hasCalibration,
                            const char* label) {
  if (!_bufferFull) {
    LOG_ERROR("Need more samples. Wait for sensor ready.");
    return;
  }

  *storage = _getMedianValue();
  *hasCalibration = true;
  float voltage = _adcToMillivolts(*storage);

  LOG_INFO("===== CALIBRATION %s =====", label);
  LOG_INFO("ADC Value: %d", *storage);
  LOG_INFO("Voltage: %.1f mV", voltage);
  LOG_INFO("Target pH: %.2f", targetPh);
  LOG_INFO("Temperature: %.1f °C", _waterTemperature);

  _saveCalibrationToNVS();
  LOG_INFO("================================");
}

void phCalibratePh686(void) {
  _calibratePoint(PH_CAL_POINT_686, &_calibVoltage686, &_hasCalib686,
                  "pH 6.86");
}

void phCalibratePh401(void) {
  _calibratePoint(PH_CAL_POINT_401, &_calibVoltage401, &_hasCalib401,
                  "pH 4.01");
}

void phCalibratePh918(void) {
  _calibratePoint(PH_CAL_POINT_918, &_calibVoltage918, &_hasCalib918,
                  "pH 9.18");
}

bool phHasCalibration401(void) { return _hasCalib401; }

bool phHasCalibration686(void) { return _hasCalib686; }

bool phHasCalibration918(void) { return _hasCalib918; }

void phClearCalibration(void) {
  _prefs.begin("phSensor", false); // Open read-write
  _prefs.clear();
  _prefs.end(); // Close to flush

  _calibVoltage401 = 0;
  _calibVoltage686 = PH_VOLTAGE_AT_686;
  _calibVoltage918 = 0;
  _lastVoltageMv = -1.0;
  _hasCalib401 = false;
  _hasCalib686 = false;
  _hasCalib918 = false;
  LOG_INFO("pH calibration cleared from NVS");
}
