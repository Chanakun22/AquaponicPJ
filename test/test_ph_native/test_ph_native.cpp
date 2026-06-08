/**
 * @file test_ph_native.cpp
 * @brief Full unit test suite for pH Sensor
 *
 * Tests: initialization, warmup, EMA filter, voltage→pH conversion,
 * temperature compensation, NaN handling, deadband, step limiter,
 * 3-point calibration, NVS persistence, legacy migration
 */

#include <unity.h>
#include <cmath>
#include <cstring>
#include <cstdint>

// ---------- Mock Arduino / ESP32 APIs before including sensor headers ----------

unsigned long _mockMillis = 0;
unsigned long millis() { return _mockMillis; }
void delay(unsigned long ms) { _mockMillis += ms; }
void delayMicroseconds(unsigned int us) { (void)us; }

// ADC mock — 12-bit (0-4095)
static int _mockAdcValue = 2048; // ~ pH 7.0
int analogRead(uint8_t) { return _mockAdcValue; }

void pinMode(uint8_t, uint8_t) {}
void analogSetAttenuation(int) {}
void Serial_print(const char*) {}
void Serial_println(const char*) {}
void Serial_printf(const char*, ...) {}

// Override Serial with no-op methods
#define Serial (MockSerial::instance())
struct MockSerial {
    static MockSerial& instance() { static MockSerial s; return s; }
    void begin(int) {}
    void print(const char*) {}
    void println(const char*) {}
    void println() {}
    void printf(const char*, ...) {}
};

// Provide LOG_LEVEL for logger.h
#define LOG_LEVEL LOG_LEVEL_INFO

// Override config.h to avoid pulling in Arduino.h again
// Define pH constants here (must match config.h)
#define PH_SAMPLE_COUNT 30
#define PH_SENSOR_PIN 6
#define ADC_11db 3
#define PH_CAL_POINT_401 4.01f
#define PH_CAL_POINT_686 6.86f
#define PH_CAL_POINT_918 9.18f
#define PH_MIN 0.0f
#define PH_MAX 14.0f
#define PH_VOLTAGE_AT_686 2058
#define PH_VOLTAGE_SLOPE -59.16f

// Stub LOG_* macros (test won't print unless we want it)
#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)

size_t telnetPrintfNonBlocking(const char*, ...) { return 0; }
void localMqttPublishLog(const char*) {}

// Replace Preferences with in-memory mock
#include "../mock_preferences.h"

void setUp(void) {}
void tearDown(void) {}

// ==================== Include the real pH source ====================
#include "adcBus.h"
#include "../../src/phSensor.cpp"

// ==================== Helper ====================

static int _phWarmupLoopCount() {
    return PH_SAMPLE_COUNT * PH_CHANNEL_COUNT;
}

static int _phLoopsBeforeReady() {
    return (PH_SAMPLE_COUNT * PH_CHANNEL_COUNT) - 2;
}

static void _advancePhLoops(int count) {
    for (int i = 0; i < count; i++) {
        _mockMillis += (unsigned long)PH_READ_INTERVAL;
        phLoop();
    }
}

static void _fastWarmup() {
    phSetup();
    adcBusResetForTest();
    _advancePhLoops(_phWarmupLoopCount());
}

// ==================== Test Cases ====================

// --- 1. Initialization ---

void test_ph_init_not_ready() {
    phClearCalibration();
    phSetup();
    TEST_ASSERT_FALSE(phIsReady());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, phRead());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, phReadVoltage());
}

// --- 2. Warmup ---

void test_ph_warmup_becomes_ready() {
    phSetup();
    TEST_ASSERT_FALSE(phIsReady());

    const int loopsBeforeReady = _phLoopsBeforeReady();
    for (int i = 0; i < loopsBeforeReady; i++) {
        _mockMillis += 1000;
        phLoop();
        TEST_ASSERT_FALSE_MESSAGE(phIsReady(), "Should not be ready before buffer full");
    }
    _mockMillis += 1000;
    phLoop();
    TEST_ASSERT_TRUE_MESSAGE(phIsReady(), "Should be ready after buffer full");
}

// --- 3. Voltage → pH (default 2-point cal, ADC=2048 → roughly pH 7) ---

void test_ph_default_conversion_neutral() {
    _mockAdcValue = 2048; // ~1.65V on 3.3V ref → 1650mV → near pH 6.86
    _fastWarmup();
    TEST_ASSERT_TRUE(phIsReady());

    float ph = phRead();
    TEST_ASSERT_TRUE_MESSAGE(!isnan(ph), "pH should not be NaN");
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 6.86f, ph);
}

// --- 4. Temperature compensation ---

void test_ph_temperature_compensation_effect() {
    // ใช้ ADC ที่ต่างจากจุดอ้างอิง 6.86 — ถ้าใกล้ reference มาก temp จะไม่เปลี่ยน pH
    _mockAdcValue = 2300;
    _fastWarmup();

    phSetTemperature(25.0f);
    _advancePhLoops(PH_CHANNEL_COUNT);
    float ph25 = phRead();

    phSetTemperature(35.0f);
    _advancePhLoops(PH_CHANNEL_COUNT);
    float ph35 = phRead();

    // pH reading should differ because Nernst slope changes with temperature
    TEST_ASSERT_TRUE_MESSAGE(fabsf(ph25 - ph35) > 0.001f,
        "Temperature compensation should affect pH reading");
}

void test_ph_temperature_compensation_per_channel() {
    _mockAdcValue = 2300;
    _fastWarmup();

    phSetTemperatureChannel(PH_CHANNEL_MIX, 25.0f);
    phSetTemperatureChannel(PH_CHANNEL_FISH, 35.0f);
    _advancePhLoops(PH_CHANNEL_COUNT);

    float phMix = phReadChannel(PH_CHANNEL_MIX);
    float phFish = phReadChannel(PH_CHANNEL_FISH);

    TEST_ASSERT_TRUE_MESSAGE(fabsf(phMix - phFish) > 0.001f,
        "Mix and fish channels should use separate compensation temperatures");
}

// --- 5. NaN handling (invalid ADC) ---

void test_ph_nan_on_invalid_adc() {
    _mockAdcValue = 2048;
    _fastWarmup();
    TEST_ASSERT_TRUE_MESSAGE(!isnan(phRead()), "Should start with valid pH");

    // Simulate sensor unplugged (ADC near 0)
    _mockAdcValue = 5; // < 10 → invalid
    for (int i = 0; i < 3; i++) {      // PH_INVALID_STREAK_LIMIT
        _mockMillis += 1000;
        phLoop();
    }
    TEST_ASSERT_TRUE_MESSAGE(isnan(phRead()), "Should be NaN after repeated invalid ADC");

    // Recover
    _mockAdcValue = 2048;
    for (int i = 0; i < PH_SAMPLE_COUNT; i++) {
        _mockMillis += 1000;
        phLoop();
    }
    TEST_ASSERT_FALSE_MESSAGE(isnan(phRead()), "Should recover after valid ADC returns");
}

// --- 6. Step limiter ---

void test_ph_step_limiter() {
    _mockAdcValue = 2048; // pH ≈ 7
    _fastWarmup();
    float phBefore = phRead();

    // Big jump in ADC → raw pH would jump significantly
    // But step limiter clamps delta to PH_PH_MAX_STEP (0.12)
    _mockAdcValue = 3000; // much higher voltage → much lower pH
    _mockMillis += 1000;
    phLoop();
    float phAfter = phRead();

    float delta = fabsf(phAfter - phBefore);
    TEST_ASSERT_TRUE_MESSAGE(delta <= 0.15f,
        "Step limiter should prevent large pH jumps per cycle");
}

// --- 7. Calibration save & load (3-point) ---

void test_ph_calibration_save_load() {
    phClearCalibration();
    phSetup();

    // Simulate calibration at pH 4.01 and 9.18 + default 6.86
    _mockAdcValue = 2500; // simulate known voltage
    _fastWarmup();
    phCalibratePh401();
    TEST_ASSERT_TRUE(phHasCalibration401());

    _mockAdcValue = 1500;
    _fastWarmup();
    phCalibratePh918();
    TEST_ASSERT_TRUE(phHasCalibration918());

    TEST_ASSERT_TRUE(phHasCalibration686()); // always present (default or calibrated)

    // Now re-initialize → should load from mock NVS
    phSetup();
    TEST_ASSERT_TRUE(phHasCalibration401());
    TEST_ASSERT_TRUE(phHasCalibration686());
    TEST_ASSERT_TRUE(phHasCalibration918());
}

// --- 8. Legacy migration (volt4/volt7 → v401/v686) ---

void test_ph_legacy_migration() {
    // Simulate legacy keys in NVS
    {
        Preferences prefs;
        prefs.begin("phSensor", false);
        prefs.putInt("volt4", 2400);
        prefs.putInt("volt7", 2000);
        prefs.remove("v401");
        prefs.remove("v686");
        prefs.remove("v918");
        prefs.end();
    }

    phSetup();
    TEST_ASSERT_TRUE_MESSAGE(phHasCalibration401(), "Should migrate legacy volt4 → v401");
    TEST_ASSERT_TRUE_MESSAGE(phHasCalibration686(), "Should migrate legacy volt7 → v686");
}

// --- 9. Clear calibration ---

void test_ph_clear_calibration() {
    _fastWarmup();
    phCalibratePh401();
    TEST_ASSERT_TRUE(phHasCalibration401());

    phClearCalibration();
    TEST_ASSERT_FALSE(phHasCalibration401());
    TEST_ASSERT_FALSE(phHasCalibration918());
    // 686 resets to default (still false because no explicit key)
    TEST_ASSERT_FALSE(phHasCalibration686());
}

// --- 10. phReadVoltage returns smoothed mV ---

void test_ph_read_voltage_after_warmup() {
    _mockAdcValue = 2048;
    _fastWarmup();
    float voltage = phReadVoltage();
    TEST_ASSERT_TRUE_MESSAGE(voltage > 0.0f, "Voltage should be positive after warmup");
    // 2048/4095 * 3300 ≈ 1650 mV
    TEST_ASSERT_FLOAT_WITHIN(100.0f, 1650.0f, voltage);
}

// --- 11. Edge: pH clamped to valid range ---

void test_ph_clamped_to_range() {
    _mockAdcValue = 4090; // ~3.298V → very low pH
    _fastWarmup();
    float ph = phRead();
    TEST_ASSERT_TRUE_MESSAGE(ph >= PH_MIN, "pH should not go below PH_MIN");
    TEST_ASSERT_TRUE_MESSAGE(ph <= PH_MAX, "pH should not exceed PH_MAX");
}

// ==================== Runner ====================

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_ph_init_not_ready);
    RUN_TEST(test_ph_warmup_becomes_ready);
    RUN_TEST(test_ph_default_conversion_neutral);
    RUN_TEST(test_ph_temperature_compensation_effect);
    RUN_TEST(test_ph_temperature_compensation_per_channel);
    RUN_TEST(test_ph_nan_on_invalid_adc);
    RUN_TEST(test_ph_step_limiter);
    RUN_TEST(test_ph_calibration_save_load);
    RUN_TEST(test_ph_legacy_migration);
    RUN_TEST(test_ph_clear_calibration);
    RUN_TEST(test_ph_read_voltage_after_warmup);
    RUN_TEST(test_ph_clamped_to_range);

    return UNITY_END();
}