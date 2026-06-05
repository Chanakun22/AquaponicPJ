/**
 * @file phSensor.cpp
 * @brief Implementation สำหรับ pH Sensor พร้อม multi-channel (mix + fish) และ NVS Storage
 */

#include "phSensor.h"
#include "config.h"
#include "logger.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>

// ============================================================================
// PRIVATE TYPES / STATE
// ============================================================================

struct PhCalibrationPoint {
    float ph;
    float voltage;
};

typedef struct {
    uint8_t pin;
    const char* name;
    const char* keyPrefix;          // NVS prefix: "mix" or "fish"

    // Acquisition state
    unsigned long lastReadTime;
    int sampleBuffer[PH_SAMPLE_COUNT];
    int sampleIndex;
    bool bufferFull;
    bool sensorReady;

    // Filtering state
    float lastPh;
    float lastVoltageMv;
    float displayVoltageMv;
    float phMovingAverage;
    float voltageMovingAverage;
    float displayVoltageMovingAverage;
    uint8_t invalidReadStreak;
    bool resetEmaRequested;

    // Calibration values (NVS-backed)
    int  calibVoltage401;
    int  calibVoltage686;
    int  calibVoltage918;
    bool hasCalib401;
    bool hasCalib686;
    bool hasCalib918;
} PhChannelState;

static Preferences _prefs;
static PhChannelState _channels[PH_CHANNEL_COUNT];
static float _waterTemperature = 25.0f;     // shared compensation temperature

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

static bool _isValidChannel(PhChannel ch) {
    return ch >= 0 && ch < PH_CHANNEL_COUNT;
}

static PhChannelState* _state(PhChannel ch) {
    return _isValidChannel(ch) ? &_channels[ch] : &_channels[PH_CHANNEL_MIX];
}

static void _initChannelDefaults(PhChannelState* st,
                                 PhChannel channel,
                                 uint8_t pin,
                                 const char* name,
                                 const char* keyPrefix) {
    memset(st, 0, sizeof(*st));
    st->pin = pin;
    st->name = name;
    st->keyPrefix = keyPrefix;
    st->sensorReady = false;
    st->bufferFull = false;
    st->lastPh = -1.0f;
    st->lastVoltageMv = -1.0f;
    st->displayVoltageMv = -1.0f;
    st->phMovingAverage = -1.0f;
    st->voltageMovingAverage = -1.0f;
    st->displayVoltageMovingAverage = -1.0f;
    st->invalidReadStreak = 0;
    st->resetEmaRequested = false;
    st->calibVoltage401 = 0;
    st->calibVoltage686 = PH_VOLTAGE_AT_686;
    st->calibVoltage918 = 0;
    st->hasCalib401 = false;
    st->hasCalib686 = false;
    st->hasCalib918 = false;
    (void)channel;
}

static void _makeKey(const PhChannelState* st, const char* suffix, char* out, size_t outSize) {
    snprintf(out, outSize, "%s_%s", st->keyPrefix, suffix);
}

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

static int _getMedianValue(const int* sampleBuffer) {
    int tempBuffer[PH_SAMPLE_COUNT];
    memcpy(tempBuffer, sampleBuffer, sizeof(tempBuffer));
    _sortArray(tempBuffer, PH_SAMPLE_COUNT);

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

static float _applyDeadband(float previousValue, float candidateValue, float deadband) {
    if (previousValue < 0 || isnan(previousValue)) {
        return candidateValue;
    }
    return fabsf(candidateValue - previousValue) < deadband ? previousValue : candidateValue;
}

static float _limitStep(float previousValue, float candidateValue, float maxStep) {
    if (previousValue < 0 || isnan(previousValue)) {
        return candidateValue;
    }
    float delta = candidateValue - previousValue;
    if (delta > maxStep)  return previousValue + maxStep;
    if (delta < -maxStep) return previousValue - maxStep;
    return candidateValue;
}

static int _readOversampledAdc(uint8_t pin) {
    long sum = 0;
    for (int i = 0; i < PH_ADC_DUMMY_READS; i++) {
        delayMicroseconds(PH_ADC_SETTLE_US);
        (void)analogRead(pin);
    }
    for (int i = 0; i < PH_OVERSAMPLE_COUNT; i++) {
        delayMicroseconds(PH_ADC_SETTLE_US);
        sum += analogRead(pin);
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

static float _calculatePhFromSegment(float voltage,
                                     float referenceVoltage,
                                     float referencePh,
                                     float slope) {
    float compensatedSlope = _compensateSlope(slope, _waterTemperature);
    return referencePh + ((voltage - referenceVoltage) / compensatedSlope);
}

static int _collectCalibrationPoints(const PhChannelState* st, PhCalibrationPoint points[]) {
    int count = 0;
    if (st->hasCalib401 && st->calibVoltage401 > 0) {
        points[count++] = { PH_CAL_POINT_401, _adcToMillivolts(st->calibVoltage401) };
    }
    points[count++] = { PH_CAL_POINT_686, _adcToMillivolts(st->calibVoltage686) };
    if (st->hasCalib918 && st->calibVoltage918 > 0) {
        points[count++] = { PH_CAL_POINT_918, _adcToMillivolts(st->calibVoltage918) };
    }
    _sortCalibrationPoints(points, count);
    return count;
}

static float _calculateSlope(float voltage1, float ph1, float voltage2, float ph2) {
    if (ph1 == ph2) return PH_VOLTAGE_SLOPE;
    return (voltage2 - voltage1) / (ph2 - ph1);
}

static float _getTemperatureCompensatedSlope(float temperature) {
    return _compensateSlope(PH_VOLTAGE_SLOPE, temperature);
}

static float _millivoltsToPh(const PhChannelState* st, float voltage) {
    PhCalibrationPoint points[3];
    int pointCount = _collectCalibrationPoints(st, points);
    float ph = NAN;

    if (pointCount >= 3) {
        bool useLowSegment =
            (voltage >= points[1].voltage && voltage <= points[0].voltage) ||
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
        float referenceVoltage = _adcToMillivolts(st->calibVoltage686);
        float slope = _getTemperatureCompensatedSlope(_waterTemperature);
        ph = PH_CAL_POINT_686 + ((voltage - referenceVoltage) / slope);
    }

    if (ph < PH_MIN) ph = PH_MIN;
    if (ph > PH_MAX) ph = PH_MAX;
    return ph;
}

// ============================================================================
// NVS — channel-aware load/save with legacy migration for MIX channel
// ============================================================================

static void _saveCalibrationToNVS(PhChannelState* st) {
    char key[20];

    _prefs.begin("phSensor", false);

    _makeKey(st, "v401", key, sizeof(key));
    if (st->hasCalib401 && st->calibVoltage401 > 0) {
        _prefs.putInt(key, st->calibVoltage401);
    } else {
        _prefs.remove(key);
    }

    _makeKey(st, "v686", key, sizeof(key));
    if (st->hasCalib686) {
        _prefs.putInt(key, st->calibVoltage686);
    } else {
        _prefs.remove(key);
    }

    _makeKey(st, "v918", key, sizeof(key));
    if (st->hasCalib918 && st->calibVoltage918 > 0) {
        _prefs.putInt(key, st->calibVoltage918);
    } else {
        _prefs.remove(key);
    }

    // First successful save of MIX channel migrates legacy keys away.
    if (st == &_channels[PH_CHANNEL_MIX]) {
        _prefs.remove("v401");
        _prefs.remove("v686");
        _prefs.remove("v918");
        _prefs.remove("volt7");
        _prefs.remove("volt4");
        _prefs.remove("slope");
    }

    _prefs.end();
    LOG_INFO("pH %s calibration saved to NVS", st->name);
}

static void _loadCalibrationFromNVS(PhChannelState* st) {
    char key[20];
    _prefs.begin("phSensor", true);

    _makeKey(st, "v401", key, sizeof(key));
    bool hasNew401 = _prefs.isKey(key);
    int  newV401   = _prefs.getInt(key, 0);

    _makeKey(st, "v686", key, sizeof(key));
    bool hasNew686 = _prefs.isKey(key);
    int  newV686   = _prefs.getInt(key, PH_VOLTAGE_AT_686);

    _makeKey(st, "v918", key, sizeof(key));
    bool hasNew918 = _prefs.isKey(key);
    int  newV918   = _prefs.getInt(key, 0);

    bool isMixChannel = (st == &_channels[PH_CHANNEL_MIX]);

    // For MIX channel: fall back to legacy unprefixed keys if prefixed ones absent.
    bool hasLegacy401 = false, hasLegacy686 = false, hasLegacy918 = false;
    int  legacyV401 = 0, legacyV686 = PH_VOLTAGE_AT_686, legacyV918 = 0;
    bool hasLegacyVolt4 = false, hasLegacyVolt7 = false;
    int  legacyVolt4 = 0, legacyVolt7 = PH_VOLTAGE_AT_686;
    if (isMixChannel) {
        hasLegacy401 = _prefs.isKey("v401");
        legacyV401   = _prefs.getInt("v401", 0);
        hasLegacy686 = _prefs.isKey("v686");
        legacyV686   = _prefs.getInt("v686", PH_VOLTAGE_AT_686);
        hasLegacy918 = _prefs.isKey("v918");
        legacyV918   = _prefs.getInt("v918", 0);
        hasLegacyVolt4 = _prefs.isKey("volt4");
        legacyVolt4    = _prefs.getInt("volt4", 0);
        hasLegacyVolt7 = _prefs.isKey("volt7");
        legacyVolt7    = _prefs.getInt("volt7", PH_VOLTAGE_AT_686);
    }
    _prefs.end();

    if (hasNew401) {
        st->hasCalib401 = true;
        st->calibVoltage401 = newV401;
    } else if (isMixChannel && hasLegacy401) {
        st->hasCalib401 = true;
        st->calibVoltage401 = legacyV401;
    } else if (isMixChannel && hasLegacyVolt4 && legacyVolt4 > 0) {
        st->hasCalib401 = true;
        st->calibVoltage401 = legacyVolt4;
        LOG_WARN("pH MIX: migrated legacy 'volt4' (pH 4.0) into pH 4.01 slot. Recalibrate with 4.01 buffer for best accuracy.");
    }

    if (hasNew686) {
        st->hasCalib686 = true;
        st->calibVoltage686 = newV686;
    } else if (isMixChannel && hasLegacy686) {
        st->hasCalib686 = true;
        st->calibVoltage686 = legacyV686;
    } else if (isMixChannel && hasLegacyVolt7) {
        st->hasCalib686 = true;
        st->calibVoltage686 = legacyVolt7;
        LOG_WARN("pH MIX: migrated legacy 'volt7' (pH 7.0) into pH 6.86 slot. Recalibrate with 6.86 buffer for best accuracy.");
    }

    if (hasNew918) {
        st->hasCalib918 = true;
        st->calibVoltage918 = newV918;
    } else if (isMixChannel && hasLegacy918) {
        st->hasCalib918 = true;
        st->calibVoltage918 = legacyV918;
    }

    LOG_INFO("pH %s calibration:", st->name);
    if (st->hasCalib401) {
        LOG_INFO("  pH%.2f: %d (%.1f mV)", PH_CAL_POINT_401, st->calibVoltage401, _adcToMillivolts(st->calibVoltage401));
    } else {
        LOG_INFO("  pH%.2f: not calibrated", PH_CAL_POINT_401);
    }
    LOG_INFO("  pH%.2f: %d (%.1f mV)%s", PH_CAL_POINT_686, st->calibVoltage686,
             _adcToMillivolts(st->calibVoltage686), st->hasCalib686 ? "" : " [default]");
    if (st->hasCalib918) {
        LOG_INFO("  pH%.2f: %d (%.1f mV)", PH_CAL_POINT_918, st->calibVoltage918, _adcToMillivolts(st->calibVoltage918));
    } else {
        LOG_INFO("  pH%.2f: not calibrated", PH_CAL_POINT_918);
    }
}

// ============================================================================
// PER-CHANNEL ACQUISITION + FILTERING
// ============================================================================

static void _processSample(PhChannelState* st) {
    int rawValue = _readOversampledAdc(st->pin);

    st->sampleBuffer[st->sampleIndex] = rawValue;
    st->sampleIndex = (st->sampleIndex + 1) % PH_SAMPLE_COUNT;

    if (st->sampleIndex == 0) {
        st->bufferFull = true;
    }
    if (!st->bufferFull) {
        return;
    }

    if (!st->sensorReady) {
        st->sensorReady = true;
        LOG_INFO("pH %s sensor ready!", st->name);
    }

    int medianValue = _getMedianValue(st->sampleBuffer);

    if (st->resetEmaRequested) {
        st->phMovingAverage = -1.0f;
        st->voltageMovingAverage = -1.0f;
        st->displayVoltageMovingAverage = -1.0f;
        st->resetEmaRequested = false;
    }

    if (!_isAdcReadingValid(medianValue)) {
        st->invalidReadStreak++;
        if (st->invalidReadStreak >= PH_INVALID_STREAK_LIMIT) {
            st->lastPh = NAN;
            st->lastVoltageMv = _adcToMillivolts(medianValue);
            st->displayVoltageMv = st->lastVoltageMv;
            st->voltageMovingAverage = st->lastVoltageMv;
            st->displayVoltageMovingAverage = st->lastVoltageMv;
        }
        return;
    }

    st->invalidReadStreak = 0;
    float rawVoltage = _adcToMillivolts(medianValue);

    if (st->voltageMovingAverage < 0 || isnan(st->voltageMovingAverage)) {
        st->voltageMovingAverage = rawVoltage;
    } else {
        st->voltageMovingAverage =
            (rawVoltage * PH_VOLTAGE_FILTER_ALPHA) +
            (st->voltageMovingAverage * (1.0f - PH_VOLTAGE_FILTER_ALPHA));
    }

    st->lastVoltageMv = _applyDeadband(st->lastVoltageMv,
                                       st->voltageMovingAverage,
                                       PH_VOLTAGE_DEADBAND_MV);

    if (st->displayVoltageMovingAverage < 0 || isnan(st->displayVoltageMovingAverage)) {
        st->displayVoltageMovingAverage = st->lastVoltageMv;
    } else {
        st->displayVoltageMovingAverage =
            (st->lastVoltageMv * PH_VOLTAGE_DISPLAY_FILTER_ALPHA) +
            (st->displayVoltageMovingAverage * (1.0f - PH_VOLTAGE_DISPLAY_FILTER_ALPHA));
    }

    st->displayVoltageMv = _applyDeadband(st->displayVoltageMv,
                                          st->displayVoltageMovingAverage,
                                          PH_VOLTAGE_DISPLAY_DEADBAND_MV);

    float rawPh = _millivoltsToPh(st, st->lastVoltageMv);
    if (isnan(rawPh)) {
        st->lastPh = NAN;
    } else if (st->phMovingAverage < 0 || isnan(st->phMovingAverage)) {
        st->phMovingAverage = rawPh;
        st->lastPh = rawPh;
    } else {
        st->phMovingAverage = (rawPh * PH_PH_FILTER_ALPHA) +
                              (st->phMovingAverage * (1.0f - PH_PH_FILTER_ALPHA));
        float stabilizedPh = _applyDeadband(st->lastPh, st->phMovingAverage, PH_PH_DEADBAND);
        st->lastPh = _limitStep(st->lastPh, stabilizedPh, PH_PH_MAX_STEP);
        st->phMovingAverage = st->lastPh;
    }
}

static void _calibratePoint(PhChannelState* st,
                            float targetPh,
                            int* storage,
                            bool* hasCalibration,
                            const char* label) {
    if (!st->bufferFull) {
        LOG_ERROR("pH %s: need more samples. Wait for sensor ready.", st->name);
        return;
    }

    *storage = _getMedianValue(st->sampleBuffer);
    *hasCalibration = true;
    float voltage = _adcToMillivolts(*storage);

    LOG_INFO("===== CALIBRATION %s [%s] =====", label, st->name);
    LOG_INFO("ADC Value: %d", *storage);
    LOG_INFO("Voltage: %.1f mV", voltage);
    LOG_INFO("Target pH: %.2f", targetPh);
    LOG_INFO("Temperature: %.1f °C", _waterTemperature);

    _saveCalibrationToNVS(st);
    st->resetEmaRequested = true;
    LOG_INFO("================================");
}

// ============================================================================
// PUBLIC API
// ============================================================================

void phSetup(void) {
    Serial.println(F("[PH] Initializing pH Sensors (multi-channel)..."));

    _initChannelDefaults(&_channels[PH_CHANNEL_MIX],  PH_CHANNEL_MIX,
                         PH_SENSOR_MIX_PIN,  "mix",  "mix");
    _initChannelDefaults(&_channels[PH_CHANNEL_FISH], PH_CHANNEL_FISH,
                         PH_SENSOR_FISH_PIN, "fish", "fish");

    for (int ch = 0; ch < PH_CHANNEL_COUNT; ch++) {
        PhChannelState* st = &_channels[ch];
        _loadCalibrationFromNVS(st);
        pinMode(st->pin, INPUT);
        LOG_INFO("pH %s sensor on GPIO %u, warming up... (%d samples needed)",
                 st->name, st->pin, PH_SAMPLE_COUNT);
    }

#if defined(ESP32)
    analogSetAttenuation(ADC_11db);
#endif
}

void phLoop(void) {
    unsigned long now = millis();
    for (int ch = 0; ch < PH_CHANNEL_COUNT; ch++) {
        PhChannelState* st = &_channels[ch];
        if (now - st->lastReadTime >= PH_READ_INTERVAL) {
            st->lastReadTime = now;
            _processSample(st);
        }
    }
}

float phReadChannel(PhChannel channel) {
    PhChannelState* st = _state(channel);
    return st->sensorReady ? st->lastPh : -1.0f;
}

float phReadVoltageChannel(PhChannel channel) {
    PhChannelState* st = _state(channel);
    if (!st->bufferFull) return -1.0f;
    return st->displayVoltageMv;
}

bool phIsReadyChannel(PhChannel channel) {
    return _state(channel)->sensorReady;
}

void phSetTemperature(float temperature) {
    if (temperature >= 0 && temperature <= 100) {
        _waterTemperature = temperature;
    }
}

void phCalibratePh686Channel(PhChannel channel) {
    PhChannelState* st = _state(channel);
    _calibratePoint(st, PH_CAL_POINT_686, &st->calibVoltage686, &st->hasCalib686, "pH 6.86");
}

void phCalibratePh401Channel(PhChannel channel) {
    PhChannelState* st = _state(channel);
    _calibratePoint(st, PH_CAL_POINT_401, &st->calibVoltage401, &st->hasCalib401, "pH 4.01");
}

void phCalibratePh918Channel(PhChannel channel) {
    PhChannelState* st = _state(channel);
    _calibratePoint(st, PH_CAL_POINT_918, &st->calibVoltage918, &st->hasCalib918, "pH 9.18");
}

bool phHasCalibration401Channel(PhChannel channel) { return _state(channel)->hasCalib401; }
bool phHasCalibration686Channel(PhChannel channel) { return _state(channel)->hasCalib686; }
bool phHasCalibration918Channel(PhChannel channel) { return _state(channel)->hasCalib918; }

void phClearCalibrationChannel(PhChannel channel) {
    PhChannelState* st = _state(channel);
    char key[20];
    _prefs.begin("phSensor", false);
    _makeKey(st, "v401", key, sizeof(key)); _prefs.remove(key);
    _makeKey(st, "v686", key, sizeof(key)); _prefs.remove(key);
    _makeKey(st, "v918", key, sizeof(key)); _prefs.remove(key);
    if (st == &_channels[PH_CHANNEL_MIX]) {
        // also clear legacy unprefixed keys
        _prefs.remove("v401");
        _prefs.remove("v686");
        _prefs.remove("v918");
        _prefs.remove("volt7");
        _prefs.remove("volt4");
        _prefs.remove("slope");
    }
    _prefs.end();

    st->calibVoltage401 = 0;
    st->calibVoltage686 = PH_VOLTAGE_AT_686;
    st->calibVoltage918 = 0;
    st->hasCalib401 = false;
    st->hasCalib686 = false;
    st->hasCalib918 = false;
    st->lastVoltageMv = -1.0f;
    st->resetEmaRequested = true;
    LOG_INFO("pH %s calibration cleared from NVS", st->name);
}
