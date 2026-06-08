/**
 * @file test_tds_native.cpp
 * @brief Full unit test suite for TDS Sensor
 *
 * Tests: initialization, warmup, median filter, voltage→ppm conversion,
 * temperature compensation, DFRobot polynomial, 2-point calibration,
 * NVS persistence, edge cases (NaN, out-of-range, invalid voltage)
 */

#include <unity.h>
#include <cmath>

// ========== Mock Arduino ==========
unsigned long _mockMillis = 0;
unsigned long millis() { return _mockMillis; }
void delay(unsigned long ms) { _mockMillis += ms; }
void delayMicroseconds(unsigned int) {}

int _mockAnalogValues[40] = {0};
int analogRead(uint8_t pin) { return _mockAnalogValues[pin]; }

void pinMode(uint8_t, uint8_t) {}
void analogSetAttenuation(int) {}

// ========== Mock Serial ==========
struct MockSerial {
    void begin(unsigned long) {}
    void print(const char*) {}
    void println(const char*) {}
    void printf(const char*, ...) {}
};
static MockSerial _mockSerial;
#define Serial _mockSerial

// ========== Mock Preferences (inline) ==========
#include <map>
#include <string>
#include <cstdlib>

static std::map<std::string, std::map<std::string, std::string>> _prefStore;

class Preferences {
    std::string _ns;
    bool _open = false;
public:
    bool begin(const char* name, bool) {
        _ns = name; _open = true; return true;
    }
    void end() { _open = false; _ns.clear(); }
    bool clear() { if (_prefStore.count(_ns)) _prefStore.erase(_ns); return true; }
    bool remove(const char* key) {
        auto it = _prefStore.find(_ns);
        if (it != _prefStore.end()) it->second.erase(key);
        return true;
    }
    bool isKey(const char* key) {
        auto it = _prefStore.find(_ns);
        if (it == _prefStore.end()) return false;
        return it->second.count(key) > 0;
    }
    int getInt(const char* key, int def = 0) {
        auto it = _prefStore.find(_ns);
        if (it == _prefStore.end()) return def;
        auto kv = it->second.find(key);
        if (kv == it->second.end()) return def;
        return std::stoi(kv->second);
    }
    float getFloat(const char* key, float def = 0.0f) {
        auto it = _prefStore.find(_ns);
        if (it == _prefStore.end()) return def;
        auto kv = it->second.find(key);
        if (kv == it->second.end()) return def;
        return std::stof(kv->second);
    }
    bool getBool(const char* key, bool def = false) {
        auto it = _prefStore.find(_ns);
        if (it == _prefStore.end()) return def;
        auto kv = it->second.find(key);
        if (kv == it->second.end()) return def;
        return kv->second == "1" || kv->second == "true";
    }
    size_t putInt(const char* key, int val) {
        _prefStore[_ns][key] = std::to_string(val);
        return sizeof(int);
    }
    size_t putFloat(const char* key, float val) {
        _prefStore[_ns][key] = std::to_string(val);
        return sizeof(float);
    }
    size_t putBool(const char* key, bool val) {
        _prefStore[_ns][key] = (val ? "1" : "0");
        return sizeof(bool);
    }
    static void resetAll() { _prefStore.clear(); }
};

// ========== portMUX (no-op on native) ==========
struct portMUX_TYPE { int _; };
#define portMUX_INITIALIZER_UNLOCKED {0}
static inline void portENTER_CRITICAL(portMUX_TYPE*) {}
static inline void portEXIT_CRITICAL(portMUX_TYPE*) {}

// ========== Logging stubs ==========
#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)

// ========== Config constants (must match config.h) ==========
#define TDS_PIN             5
#define TDS_VREF            3.3f
#define TDS_ADC_RESOLUTION  4096.0f
#define TDS_SAMPLE_COUNT    30
#define TDS_READ_INTERVAL   1000
#define TDS_CONVERSION_FACTOR 0.695f
#define TDS_MIN             0.0f
#define TDS_MAX             2000.0f

#ifndef isnan
#define isnan(x) std::isnan(x)
#endif

static float _mockTemperatureCoefficient(float temperature) {
    return 1.0f + 0.019f * (temperature - 25.0f);
}

static float _mockStandardTds(float voltage) {
    if (voltage <= 0.0f) {
        return 0.0f;
    }
    return (133.42f * voltage * voltage * voltage
          - 255.86f * voltage * voltage
          + 857.39f * voltage) * TDS_CONVERSION_FACTOR;
}

static int _adcFromVoltage(float voltage) {
    return static_cast<int>((voltage * TDS_ADC_RESOLUTION / TDS_VREF) + 0.5f);
}

// ========== Include real TDS source ==========
#include "adcBus.h"
#include "../../src/TdsSensor.cpp"

// ========== Helper: feed samples to warm up ==========
static int _tdsWarmupLoopCount() {
#if TDS_FISH_CHANNEL_ENABLED
    return TDS_SAMPLE_COUNT * TDS_CHANNEL_COUNT;
#else
    return TDS_SAMPLE_COUNT;
#endif
}

static int _tdsLoopsBeforeReady() {
#if TDS_FISH_CHANNEL_ENABLED
    return (TDS_SAMPLE_COUNT * TDS_CHANNEL_COUNT) - 2;
#else
    return TDS_SAMPLE_COUNT - 1;
#endif
}

static void _fastWarmup(float temperature = 25.0f) {
    tdsSetup();
    // Round-robin reads one channel per loop — mix needs TDS_SAMPLE_COUNT mix reads
    for (int i = 0; i < _tdsWarmupLoopCount(); i++) {
        _mockMillis += TDS_READ_INTERVAL;
        tdsLoop(temperature);
    }
}

// ========== Test Cases ==========

// --- 1. Initialization ---

void test_tds_init_not_ready() {
    Preferences::resetAll();
    tdsSetup();
    TEST_ASSERT_FALSE(tdsIsReady());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, tdsGetLastValue());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, tdsGetVoltage());
}

// --- 2. Warmup ---

void test_tds_warmup_becomes_ready() {
    Preferences::resetAll();
    tdsSetup();
    const int loopsBeforeReady = _tdsLoopsBeforeReady();
    for (int i = 0; i < loopsBeforeReady; i++) {
        _mockMillis += TDS_READ_INTERVAL;
        tdsLoop(25.0f);
        TEST_ASSERT_FALSE(tdsIsReady());
    }
    _mockMillis += TDS_READ_INTERVAL;
    tdsLoop(25.0f);
    TEST_ASSERT_TRUE(tdsIsReady());
}

// --- 3. Default conversion factor on known voltage ---

void test_tds_polynomial_known_voltage() {
    Preferences::resetAll();
    // ADC value corresponding to ~1.414V on the analog output.
    _mockAnalogValues[TDS_PIN] = 1756;
    _fastWarmup(25.0f);

    TEST_ASSERT_TRUE(tdsIsReady());
    float tds = tdsGetLastValue();

    TEST_ASSERT_TRUE(!isnan(tds));
    TEST_ASSERT_TRUE(tds >= TDS_MIN && tds <= TDS_MAX);
    TEST_ASSERT_FLOAT_WITHIN(200.0f, 749.0f, tds);
}

// --- 4. Temperature compensation ---

void test_tds_temp_compensation() {
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 1756;

    _fastWarmup(25.0f);
    float tds25 = tdsGetLastValue();

    // Re-warmup with same ADC but different temp
    _mockAnalogValues[TDS_PIN] = 1756;
    _fastWarmup(35.0f);
    float tds35 = tdsGetLastValue();

    // compCoeff: 1 + 0.019*(T-25)
    // At 25°C: coeff=1.0, voltage=raw
    // At 35°C: coeff=1.19, voltage = raw/1.19 → lower → lower TDS
    TEST_ASSERT_TRUE(tds35 < tds25);
}

// --- 5. NaN on temperature NaN ---

void test_tds_nan_on_temp_nan() {
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 1756;
    _fastWarmup(NAN);

    TEST_ASSERT_TRUE(isnan(tdsGetLastValue()));
}

// --- 6. NaN on unplugged (voltage near 0) ---

void test_tds_nan_on_unplugged() {
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 3; // ~2.4mV → < 50mV threshold
    _fastWarmup(25.0f);

    TEST_ASSERT_TRUE(isnan(tdsGetLastValue()));
}

// --- 7. NaN on short circuit (voltage near VREF) ---

void test_tds_nan_on_short() {
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 4090; // ~3.297V → > (3.3-0.05)V = 3.25V
    _fastWarmup(25.0f);

    TEST_ASSERT_TRUE(isnan(tdsGetLastValue()));
}

// --- 8. Calibration save & load ---

void test_tds_calibration_save_load() {
    Preferences::resetAll();

    // Set calibration
    tdsSetup();
    tdsSetCalibration(800.0f, 1.5f, 25.0f, 1413.0f, 2.0f, 25.0f, false);
    TEST_ASSERT_TRUE(tdsIsCalibrated());

    // Re-init → should load from mock NVS
    tdsSetup();
    TEST_ASSERT_TRUE(tdsIsCalibrated());
}

// --- 9. Calibration rejection (same voltage) ---

void test_tds_calibration_reject_equal_voltage() {
    Preferences::resetAll();
    tdsSetup();
    tdsSetCalibration(100.0f, 1.0f, 25.0f, 500.0f, 1.0f, 25.0f, false);  // same voltage
    TEST_ASSERT_FALSE(tdsIsCalibrated());
}

void test_tds_calibration_reject_small_voltage_span() {
    Preferences::resetAll();
    tdsSetup();
    tdsSetCalibration(500.0f, 0.140f, 25.0f, 982.0f, 0.167f, 25.0f, false);
    TEST_ASSERT_FALSE(tdsIsCalibrated());
}

void test_tds_calibration_normalizes_standard_temperature() {
    Preferences::resetAll();

    const float calibrationTemperature = 35.0f;
    const float lowReferenceVoltage = 1.00f;
    const float highReferenceVoltage = 1.60f;
    const float lowRawVoltage = lowReferenceVoltage * _mockTemperatureCoefficient(calibrationTemperature);
    const float highRawVoltage = highReferenceVoltage * _mockTemperatureCoefficient(calibrationTemperature);
    const float lowReferencePpm = _mockStandardTds(lowReferenceVoltage);
    const float highReferencePpm = _mockStandardTds(highReferenceVoltage);

    tdsSetup();
    tdsSetCalibration(
        lowReferencePpm,
        lowRawVoltage,
        calibrationTemperature,
        highReferencePpm,
        highRawVoltage,
        calibrationTemperature,
        true
    );
    TEST_ASSERT_TRUE(tdsIsCalibrated());

    _mockAnalogValues[TDS_PIN] = _adcFromVoltage(lowRawVoltage);
    _fastWarmup(calibrationTemperature);
    TEST_ASSERT_FLOAT_WITHIN(20.0f, lowReferencePpm, tdsGetLastValue());

    Preferences::resetAll();
    tdsSetup();
    tdsSetCalibration(
        lowReferencePpm,
        lowRawVoltage,
        calibrationTemperature,
        highReferencePpm,
        highRawVoltage,
        calibrationTemperature,
        true
    );
    _mockAnalogValues[TDS_PIN] = _adcFromVoltage(highRawVoltage);
    _fastWarmup(calibrationTemperature);
    TEST_ASSERT_FLOAT_WITHIN(20.0f, highReferencePpm, tdsGetLastValue());
}

// --- 10. Moving average smoothing ---

void test_tds_moving_average_smoothing() {
    Preferences::resetAll();

    // Warm up with stable value
    _mockAnalogValues[TDS_PIN] = 1756;
    _fastWarmup(25.0f);
    float stableVal = tdsGetLastValue();
    TEST_ASSERT_TRUE(!isnan(stableVal));

    // Abrupt change
    _mockAnalogValues[TDS_PIN] = 3000; // much higher voltage → higher TDS
    _mockMillis += TDS_READ_INTERVAL;
    tdsLoop(25.0f);
    float afterOneSample = tdsGetLastValue();

    // Stabilization layer should prevent a single noisy sample from jumping too far.
    float delta = fabsf(afterOneSample - stableVal);
    TEST_ASSERT_TRUE(delta <= (TDS_MIX_VALUE_MAX_STEP_PPM + 0.01f)); // config.h mix max step
}

// --- 11. TDS clamped to valid range ---

void test_tds_clamped_to_range() {
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 4090; // would produce high TDS if not clamped
    _fastWarmup(25.0f);

    // Should be NaN (voltage too high → unplugged detection), not a high value
    // Let's use a valid but high voltage
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 3500; // ~2.82V → high TDS
    _fastWarmup(25.0f);
    float tds = tdsGetLastValue();
    if (!isnan(tds)) {
        TEST_ASSERT_TRUE(tds <= TDS_MAX);
    }
}

// --- 12. tdsRead() directly works ---

void test_tds_direct_tdsRead() {
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 1756;
    tdsSetup();

    // Call tdsRead directly (not through tdsLoop)
    for (int i = 0; i < TDS_SAMPLE_COUNT; i++) {
        float val = tdsRead(25.0f);
        (void)val;
    }
    float result = tdsRead(25.0f);
    TEST_ASSERT_TRUE(!isnan(result));
    TEST_ASSERT_TRUE(result > 0.0f);
}

// --- 13. tdsLoop respects interval timing ---

void test_tds_loop_interval_respect() {
    Preferences::resetAll();
    _mockAnalogValues[TDS_PIN] = 1756;
    tdsSetup();

    // Feed samples through tdsLoop with proper intervals
    for (int i = 0; i < _tdsWarmupLoopCount(); i++) {
        _mockMillis += TDS_READ_INTERVAL;
        tdsLoop(25.0f);
    }
    float afterInterval = tdsGetLastValue();
    TEST_ASSERT_TRUE(!isnan(afterInterval));

    // Call again immediately without advancing millis → should NOT update
    float snapshot = tdsGetLastValue();
    tdsLoop(25.0f); // No time passed → should skip
    TEST_ASSERT_EQUAL_FLOAT(snapshot, tdsGetLastValue());
}

// ==================== Runner ====================

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_tds_init_not_ready);
    RUN_TEST(test_tds_warmup_becomes_ready);
    RUN_TEST(test_tds_polynomial_known_voltage);
    RUN_TEST(test_tds_temp_compensation);
    RUN_TEST(test_tds_nan_on_temp_nan);
    RUN_TEST(test_tds_nan_on_unplugged);
    RUN_TEST(test_tds_nan_on_short);
    RUN_TEST(test_tds_calibration_save_load);
    RUN_TEST(test_tds_calibration_reject_equal_voltage);
    RUN_TEST(test_tds_calibration_reject_small_voltage_span);
    RUN_TEST(test_tds_calibration_normalizes_standard_temperature);
    RUN_TEST(test_tds_moving_average_smoothing);
    RUN_TEST(test_tds_clamped_to_range);
    RUN_TEST(test_tds_direct_tdsRead);
    RUN_TEST(test_tds_loop_interval_respect);

    return UNITY_END();
}