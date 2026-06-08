/**
 * @file TdsSensor.cpp
 * @brief Implementation สำหรับ TDS Sensor พร้อม 2-Point Calibration + NVS Storage
 */

#include "TdsSensor.h"
#include "adcBus.h"
#include "logger.h"
#include "config.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>

// ============================================================================
// PRIVATE TYPES / STATE
// ============================================================================

typedef struct {
    uint8_t pin;
    const char* name;
    const char* keyPrefix;
    unsigned long lastReadTime;
    int buffer[TDS_SAMPLE_COUNT];
    int bufferIndex;
    int sampleCollected;
    bool ready;
    float lastResult;
    float lastVoltage;
    float voltageMovingAverage;
    float movingAverage;
    float kValue;
    float offset;
    bool isCalibrated;
    float calLowPpm;
    float calLowVoltage;
    float calLowTemperature;
    float calHighPpm;
    float calHighVoltage;
    float calHighTemperature;
    bool calibrationUsesRawVoltage;
} TdsChannelState;

static Preferences _tdsPrefs;
static uint8_t _tdsNextChannelIndex = 0;
static TdsChannelState _channels[TDS_CHANNEL_COUNT] = {
    { TDS_MIX_PIN,  "mix",  "mix",  0, {0}, 0, 0, false, -1.0f, -1.0f, -1.0f, -1.0f,
      1.0f, 0.0f, false, 0.0f, 0.0f, 25.0f, 0.0f, 0.0f, 25.0f, false },
    { TDS_FISH_PIN, "fish", "fish", 0, {0}, 0, 0, false, -1.0f, -1.0f, -1.0f, -1.0f,
      1.0f, 0.0f, false, 0.0f, 0.0f, 25.0f, 0.0f, 0.0f, 25.0f, false },
};

#if defined(ESP32)
static portMUX_TYPE _tdsMux = portMUX_INITIALIZER_UNLOCKED;
#define TDS_LOCK() portENTER_CRITICAL(&_tdsMux)
#define TDS_UNLOCK() portEXIT_CRITICAL(&_tdsMux)
#else
#define TDS_LOCK()
#define TDS_UNLOCK()
#endif

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

static bool _isValidChannel(TdsChannel channel) {
    return channel >= 0 && channel < TDS_CHANNEL_COUNT;
}

static TdsChannelState* _state(TdsChannel channel) {
    return _isValidChannel(channel) ? &_channels[channel] : &_channels[TDS_CHANNEL_MIX];
}

static void _makeKey(const TdsChannelState* st, const char* suffix, char* out, size_t outSize) {
    snprintf(out, outSize, "%s_%s", st->keyPrefix, suffix);
}

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
    float normalizedHighVoltage = _normalizeCalibrationVoltage(highVoltage, highTemperature,
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

static float _channelVoltageFilterAlpha(TdsChannel channel) {
    return (channel == TDS_CHANNEL_MIX) ? TDS_MIX_VOLTAGE_FILTER_ALPHA : TDS_VOLTAGE_FILTER_ALPHA;
}

static float _channelVoltageDeadband(TdsChannel channel) {
    return (channel == TDS_CHANNEL_MIX) ? TDS_MIX_VOLTAGE_DEADBAND_V : TDS_VOLTAGE_DEADBAND_V;
}

static float _channelValueFilterAlpha(TdsChannel channel) {
    return (channel == TDS_CHANNEL_MIX) ? TDS_MIX_VALUE_FILTER_ALPHA : TDS_VALUE_FILTER_ALPHA;
}

static float _channelValueDeadband(TdsChannel channel) {
    return (channel == TDS_CHANNEL_MIX) ? TDS_MIX_VALUE_DEADBAND_PPM : TDS_VALUE_DEADBAND_PPM;
}

static float _channelValueMaxStep(TdsChannel channel) {
    return (channel == TDS_CHANNEL_MIX) ? TDS_MIX_VALUE_MAX_STEP_PPM : TDS_VALUE_MAX_STEP_PPM;
}

static bool _shouldSampleChannel(TdsChannel channel) {
    if (channel != TDS_CHANNEL_FISH) {
        return true;
    }

    TdsChannelState* st = &_channels[TDS_CHANNEL_FISH];
    if (!st->ready) {
        return true;
    }

    float voltage = st->lastVoltage;
    if (voltage < 0.0f) {
        return true;
    }

    // ข้าม fish probe ที่ลอย/ไม่ต่อ — ลด ADC noise ที่รบกวน mix
    return voltage >= 0.05f && voltage <= (TDS_VREF - 0.05f);
}

static int _oversampleCountForChannel(TdsChannel channel) {
    return (channel == TDS_CHANNEL_MIX) ? TDS_MIX_OVERSAMPLE_COUNT : TDS_FISH_OVERSAMPLE_COUNT;
}

static int _readOversampledAdc(TdsChannel channel, uint8_t pin) {
    return adcBusReadOversampled(pin,
                                 TDS_ADC_DUMMY_READS,
                                 _oversampleCountForChannel(channel),
                                 TDS_ADC_SETTLE_US);
}

static int _getMedian(int* bArray, int iFilterLen) {
    int bTab[iFilterLen];
    for (byte i = 0; i < iFilterLen; i++) {
        bTab[i] = bArray[i];
    }

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

    if ((iFilterLen & 1) > 0) {
        bTemp = bTab[(iFilterLen - 1) / 2];
    } else {
        bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
    }

    return bTemp;
}

static void _calculateCalibrationFactors(TdsChannelState* st) {
    float normalizedLowVoltage = _normalizeCalibrationVoltage(st->calLowVoltage,
                                                              st->calLowTemperature,
                                                              st->calibrationUsesRawVoltage);
    float normalizedHighVoltage = _normalizeCalibrationVoltage(st->calHighVoltage,
                                                               st->calHighTemperature,
                                                               st->calibrationUsesRawVoltage);

    if (!_hasValidCalibrationSpan(st->calLowVoltage,
                                  st->calLowTemperature,
                                  st->calHighVoltage,
                                  st->calHighTemperature,
                                  st->calibrationUsesRawVoltage)) {
        st->kValue = 1.0f;
        st->offset = 0.0f;
        st->isCalibrated = false;
        LOG_ERROR("TDS %s calibration rejected: voltage span %.3fV is too small (need >= %.3fV)",
                  st->name,
                  fabsf(normalizedHighVoltage - normalizedLowVoltage),
                  TDS_MIN_CALIBRATION_SPAN_V);
        return;
    }

    float baseLow = _getStandardTds(normalizedLowVoltage);
    float baseHigh = _getStandardTds(normalizedHighVoltage);
    if (fabsf(baseHigh - baseLow) < 0.0001f) {
        st->kValue = 1.0f;
        st->offset = 0.0f;
        return;
    }

    st->kValue = (st->calHighPpm - st->calLowPpm) / (baseHigh - baseLow);
    st->offset = st->calLowPpm - (st->kValue * baseLow);

    LOG_INFO("TDS %s calibrated K: %.4f, Offset: %.4f", st->name, st->kValue, st->offset);
}

static void _loadCalibrationFromNVS(TdsChannelState* st) {
    char key[20];
    _tdsPrefs.begin("tdsSensor", true);

    // Legacy keys are the mix channel. If namespaced keys do not exist yet, keep reading old keys.
    _makeKey(st, "cal", key, sizeof(key));
    st->isCalibrated = _tdsPrefs.getBool(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getBool("calibrated", false) : false);

    if (st->isCalibrated) {
        _makeKey(st, "lowPpm", key, sizeof(key));
        st->calLowPpm = _tdsPrefs.getFloat(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getFloat("lowPpm", 0) : 0);
        _makeKey(st, "lowV", key, sizeof(key));
        st->calLowVoltage = _tdsPrefs.getFloat(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getFloat("lowV", 0) : 0);
        _makeKey(st, "lowT", key, sizeof(key));
        st->calLowTemperature = _tdsPrefs.getFloat(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getFloat("lowT", 25.0f) : 25.0f);
        _makeKey(st, "highPpm", key, sizeof(key));
        st->calHighPpm = _tdsPrefs.getFloat(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getFloat("highPpm", 0) : 0);
        _makeKey(st, "highV", key, sizeof(key));
        st->calHighVoltage = _tdsPrefs.getFloat(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getFloat("highV", 0) : 0);
        _makeKey(st, "highT", key, sizeof(key));
        st->calHighTemperature = _tdsPrefs.getFloat(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getFloat("highT", 25.0f) : 25.0f);
        _makeKey(st, "rawV", key, sizeof(key));
        st->calibrationUsesRawVoltage = _tdsPrefs.getBool(key, st == &_channels[TDS_CHANNEL_MIX] ? _tdsPrefs.getBool("rawV", false) : false);
        _calculateCalibrationFactors(st);
    }
    _tdsPrefs.end();
}

static void _saveCalibrationToNVS(TdsChannelState* st) {
    char key[20];
    _tdsPrefs.begin("tdsSensor", false);
    _makeKey(st, "cal", key, sizeof(key));
    _tdsPrefs.putBool(key, st->isCalibrated);
    _makeKey(st, "lowPpm", key, sizeof(key));
    _tdsPrefs.putFloat(key, st->calLowPpm);
    _makeKey(st, "lowV", key, sizeof(key));
    _tdsPrefs.putFloat(key, st->calLowVoltage);
    _makeKey(st, "lowT", key, sizeof(key));
    _tdsPrefs.putFloat(key, st->calLowTemperature);
    _makeKey(st, "highPpm", key, sizeof(key));
    _tdsPrefs.putFloat(key, st->calHighPpm);
    _makeKey(st, "highV", key, sizeof(key));
    _tdsPrefs.putFloat(key, st->calHighVoltage);
    _makeKey(st, "highT", key, sizeof(key));
    _tdsPrefs.putFloat(key, st->calHighTemperature);
    _makeKey(st, "rawV", key, sizeof(key));
    _tdsPrefs.putBool(key, st->calibrationUsesRawVoltage);
    _tdsPrefs.end();
    LOG_INFO("TDS %s calibration saved to NVS", st->name);
}

static float _calculateTdsFromVoltage(TdsChannelState* st, float voltage) {
    float baseTds = _getStandardTds(voltage);
    return st->isCalibrated ? (st->kValue * baseTds) + st->offset : baseTds;
}

static float _calculateFilteredVoltage(TdsChannel channel) {
    TdsChannelState* st = _state(channel);
    int tempBuffer[TDS_SAMPLE_COUNT];
    bool ready = false;
    float previousVoltage = -1.0f;

    TDS_LOCK();
    ready = st->ready;
    previousVoltage = st->lastVoltage;
    memcpy(tempBuffer, st->buffer, sizeof(tempBuffer));
    TDS_UNLOCK();

    if (!ready) {
        return -1.0f;
    }

    float rawVoltage = _getMedian(tempBuffer, TDS_SAMPLE_COUNT) * TDS_VREF / TDS_ADC_RESOLUTION;

    float voltageAlpha = _channelVoltageFilterAlpha(channel);
    float voltageDeadband = _channelVoltageDeadband(channel);

    if (st->voltageMovingAverage < 0 || isnan(st->voltageMovingAverage)) {
        st->voltageMovingAverage = rawVoltage;
    } else {
        st->voltageMovingAverage =
            (rawVoltage * voltageAlpha) +
            (st->voltageMovingAverage * (1.0f - voltageAlpha));
    }

    return _applyDeadband(previousVoltage, st->voltageMovingAverage, voltageDeadband);
}

static void _resetRuntimeState(TdsChannelState* st) {
    memset(st->buffer, 0, sizeof(st->buffer));
    st->bufferIndex = 0;
    st->sampleCollected = 0;
    st->ready = false;
    st->lastResult = -1.0f;
    st->lastVoltage = -1.0f;
    st->voltageMovingAverage = -1.0f;
    st->movingAverage = -1.0f;
    st->lastReadTime = 0;
}

static float _tdsReadChannel(TdsChannel channel, float temperature);

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void tdsSetup(void) {
    _tdsNextChannelIndex = 0;
    adcBusSetup();

    for (int ch = 0; ch < TDS_CHANNEL_COUNT; ch++) {
        TdsChannelState* st = &_channels[ch];
        pinMode(st->pin, INPUT);
        _loadCalibrationFromNVS(st);
        _resetRuntimeState(st);

        LOG_INFO("TDS %s sensor initialized on GPIO %u, collecting %d samples...",
                 st->name, st->pin, TDS_SAMPLE_COUNT);

        if (st->isCalibrated) {
            LOG_INFO("TDS %s calibration loaded: Low=%.0f ppm @ %.3fV / %.1fC, High=%.0f ppm @ %.3fV / %.1fC, mode=%s",
                     st->name, st->calLowPpm, st->calLowVoltage, st->calLowTemperature,
                     st->calHighPpm, st->calHighVoltage, st->calHighTemperature,
                     st->calibrationUsesRawVoltage ? "raw" : "compensated");
        } else {
            LOG_WARN("TDS %s not calibrated, using default formula", st->name);
        }
    }

}

float tdsGetVoltageForChannel(TdsChannel channel) {
    TdsChannelState* st = _state(channel);
    float voltage = -1.0f;
    TDS_LOCK();
    voltage = st->lastVoltage;
    TDS_UNLOCK();
    return voltage;
}

float tdsGetVoltage(void) {
    return tdsGetVoltageForChannel(TDS_CHANNEL_MIX);
}

bool tdsIsReady(void) {
    return tdsIsReadyForChannel(TDS_CHANNEL_MIX);
}

bool tdsIsReadyForChannel(TdsChannel channel) {
    TdsChannelState* st = _state(channel);
    bool ready = false;
    TDS_LOCK();
    ready = st->ready;
    TDS_UNLOCK();
    return ready;
}

bool tdsIsCalibratedForChannel(TdsChannel channel) {
    return _state(channel)->isCalibrated;
}

bool tdsIsCalibrated(void) {
    return tdsIsCalibratedForChannel(TDS_CHANNEL_MIX);
}

float tdsRead(float temperature) {
    return _tdsReadChannel(TDS_CHANNEL_MIX, temperature);
}

static float _tdsReadChannel(TdsChannel channel, float temperature) {
    TdsChannelState* st = _state(channel);
    int sample = _readOversampledAdc(channel, st->pin);
    bool becameReady = false;
    float previousTds = -1.0f;

    TDS_LOCK();
    previousTds = st->lastResult;
    st->buffer[st->bufferIndex] = sample;
    st->bufferIndex = (st->bufferIndex + 1) % TDS_SAMPLE_COUNT;

    if (!st->ready && st->bufferIndex == 0) {
        st->ready = true;
        becameReady = true;
    }
    TDS_UNLOCK();

    if (becameReady) {
        LOG_INFO("TDS %s buffer full, sensor ready!", st->name);
    }

    if (!st->ready) return -1.0f;

    float voltage = _calculateFilteredVoltage(channel);
    TDS_LOCK();
    st->lastVoltage = voltage;
    TDS_UNLOCK();

    if (voltage < 0.0f) return -1.0f;

    if (voltage < 0.05f || voltage > (TDS_VREF - 0.05f)) {
        TDS_LOCK();
        st->lastResult = NAN;
        TDS_UNLOCK();
        return NAN;
    }

    if (isnan(temperature)) {
        TDS_LOCK();
        st->lastResult = NAN;
        TDS_UNLOCK();
        return NAN;
    }

    float compensationVoltage = _getCompensatedVoltage(voltage, temperature);
    float tdsValue = _calculateTdsFromVoltage(st, compensationVoltage);

    if (tdsValue < TDS_MIN) tdsValue = TDS_MIN;
    if (tdsValue > TDS_MAX) tdsValue = TDS_MAX;

    float valueAlpha = _channelValueFilterAlpha(channel);
    float valueDeadband = _channelValueDeadband(channel);
    float valueMaxStep = _channelValueMaxStep(channel);

    if (st->movingAverage < 0 || isnan(st->movingAverage)) {
        st->movingAverage = tdsValue;
    } else {
        st->movingAverage =
            (tdsValue * valueAlpha) +
            (st->movingAverage * (1.0f - valueAlpha));
        float stabilizedTds = _applyDeadband(previousTds, st->movingAverage, valueDeadband);
        st->movingAverage = _limitStep(previousTds, stabilizedTds, valueMaxStep);
    }

    TDS_LOCK();
    st->lastResult = st->movingAverage;
    TDS_UNLOCK();
    return st->movingAverage;
}

float tdsGetLastValueForChannel(TdsChannel channel) {
    TdsChannelState* st = _state(channel);
    float lastValue = -1.0f;
    TDS_LOCK();
    lastValue = st->lastResult;
    TDS_UNLOCK();
    return lastValue;
}

float tdsGetLastValue(void) {
    return tdsGetLastValueForChannel(TDS_CHANNEL_MIX);
}

void tdsSetCalibrationForChannel(TdsChannel channel,
                                 float lowPpm,
                                 float lowVoltage,
                                 float lowTemperature,
                                 float highPpm,
                                 float highVoltage,
                                 float highTemperature,
                                 bool rawVoltageInput) {
    TdsChannelState* st = _state(channel);
    st->calLowPpm = lowPpm;
    st->calLowVoltage = lowVoltage;
    st->calLowTemperature = isfinite(lowTemperature) ? lowTemperature : 25.0f;
    st->calHighPpm = highPpm;
    st->calHighVoltage = highVoltage;
    st->calHighTemperature = isfinite(highTemperature) ? highTemperature : 25.0f;
    st->calibrationUsesRawVoltage = rawVoltageInput;

    if (_hasValidCalibrationSpan(st->calLowVoltage,
                                 st->calLowTemperature,
                                 st->calHighVoltage,
                                 st->calHighTemperature,
                                 st->calibrationUsesRawVoltage)) {
        _calculateCalibrationFactors(st);
        st->isCalibrated = true;
        _saveCalibrationToNVS(st);

        st->movingAverage = -1.0f;
        st->voltageMovingAverage = -1.0f;

        LOG_INFO("TDS %s calibration set (Hybrid Mode):", st->name);
        LOG_INFO("  K: %.4f, Offset: %.4f, LowTemp: %.1fC, HighTemp: %.1fC, mode=%s",
                 st->kValue, st->offset, st->calLowTemperature, st->calHighTemperature,
                 st->calibrationUsesRawVoltage ? "raw" : "compensated");
    } else {
        float normalizedLowVoltage = _normalizeCalibrationVoltage(st->calLowVoltage,
                                                                  st->calLowTemperature,
                                                                  st->calibrationUsesRawVoltage);
        float normalizedHighVoltage = _normalizeCalibrationVoltage(st->calHighVoltage,
                                                                   st->calHighTemperature,
                                                                   st->calibrationUsesRawVoltage);
        LOG_ERROR("TDS %s calibration failed: voltage span %.3fV is too small (need >= %.3fV)",
                  st->name,
                  fabsf(normalizedHighVoltage - normalizedLowVoltage),
                  TDS_MIN_CALIBRATION_SPAN_V);
        st->isCalibrated = false;
    }
}

void tdsSetCalibration(float lowPpm,
                       float lowVoltage,
                       float lowTemperature,
                       float highPpm,
                       float highVoltage,
                       float highTemperature,
                       bool rawVoltageInput) {
    tdsSetCalibrationForChannel(TDS_CHANNEL_MIX,
                                lowPpm,
                                lowVoltage,
                                lowTemperature,
                                highPpm,
                                highVoltage,
                                highTemperature,
                                rawVoltageInput);
}

void tdsLoop(float temperature) {
    tdsLoopChannels(temperature, temperature);
}

void tdsLoopChannels(float mixTemperature, float fishTemperature) {
    unsigned long now = millis();

#if !TDS_FISH_CHANNEL_ENABLED
    TdsChannelState* mixState = &_channels[TDS_CHANNEL_MIX];
    if (now - mixState->lastReadTime >= TDS_READ_INTERVAL) {
        mixState->lastReadTime = now;
        _tdsReadChannel(TDS_CHANNEL_MIX, mixTemperature);
    }
    return;
#endif

    // อ่านทีละ channel ต่อรอบ เพื่อลด ADC crosstalk ระหว่าง mix (GPIO5) กับ fish (GPIO7)
    for (int offset = 0; offset < TDS_CHANNEL_COUNT; offset++) {
        int ch = (_tdsNextChannelIndex + offset) % TDS_CHANNEL_COUNT;
        TdsChannel channel = (TdsChannel)ch;
        if (!_shouldSampleChannel(channel)) {
            continue;
        }

        TdsChannelState* st = &_channels[ch];
        if (now - st->lastReadTime < TDS_READ_INTERVAL) {
            continue;
        }

        st->lastReadTime = now;
        float channelTemp = (ch == TDS_CHANNEL_FISH) ? fishTemperature : mixTemperature;
        _tdsReadChannel((TdsChannel)ch, channelTemp);
        _tdsNextChannelIndex = (ch + 1) % TDS_CHANNEL_COUNT;
        break;
    }
}
