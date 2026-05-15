/**
 * @file test_temp_native.cpp
 * @brief Full unit test suite for DS18B20 Temperature Sensor
 *
 * Tests: initialization, async state machine, temperature read,
 * retry on failure, max retry exhaustion, non-blocking timing,
 * NAN return on persistent failure
 */

#include <unity.h>
#include <cmath>

// ========== Mock Arduino ==========
unsigned long _mockMillis = 0;
unsigned long millis() { return _mockMillis; }
void delay(unsigned long ms) { _mockMillis += ms; }
void delayMicroseconds(unsigned int) {}

int analogRead(uint8_t) { return 0; }
void pinMode(uint8_t, uint8_t) {}
void analogSetAttenuation(int) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return 0; }

// ========== Mock Serial ==========
struct MockSerial {
    void begin(unsigned long) {}
    void print(const char*) {}
    void println(const char*) {}
    void println() {}
    void printf(const char*, ...) {}
};
MockSerial Serial;

// ========== Logging stubs ==========
#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL LOG_LEVEL_INFO

// ========== Config constants ==========
#define ONE_WIRE_PIN        13
#define TEMP_READ_INTERVAL  2000
#define I2C_SDA_PIN         8
#define I2C_SCL_PIN         9
#define DHT_PIN             15
#define DHT_TYPE            DHT22

// ========== String/F macros ==========
class String {
public:
    String() {}
    String(const char*) {}
    operator const char*() const { return ""; }
};
#define F(str) (str)

// ========== FP helpers ==========
#ifndef isnan
#define isnan(x) std::isnan(x)
#endif

// ========== Include mocks before source ==========
#include "mock/OneWire.h"
#include "mock/DallasTemperature.h"

// ========== Static mock definitions ==========
float DallasTemperature::_mockTemp = 25.0f;

// ========== Include real temp source ==========
#include "../../src/tempSensor.cpp"

// ========== Test Cases ==========

// --- 1. Initial state: temp is NAN before first loop ---

void test_temp_initial_nan() {
    _mockMillis = 0;
    DallasTemperature::setMockTemperature(25.0f);
    tempSetup();

    // tempRead should return NAN initially
    float t = tempRead();
    TEST_ASSERT_TRUE_MESSAGE(isnan(t), "Temperature should be NaN before first loop");
}

// --- 2. Normal read cycle: request -> wait -> value ---

void test_temp_normal_read() {
    _mockMillis = 0;
    DallasTemperature::setMockTemperature(26.5f);
    tempSetup();

    // First loop: start request (TEMP_IDLE -> TEMP_WAITING)
    _mockMillis = TEMP_READ_INTERVAL;
    tempLoop();

    // tempRead still NAN (conversion not done yet)
    TEST_ASSERT_TRUE_MESSAGE(isnan(tempRead()), "Temperature should still be NaN during conversion");

    // Advance past conversion delay + some margin
    _mockMillis += 900;
    tempLoop();

    float t = tempRead();
    TEST_ASSERT_FALSE_MESSAGE(isnan(t), "Temperature should be valid after conversion");
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 26.5f, t);
}

// --- 3. Non-blocking: no request if interval not elapsed ---

void test_temp_nonblocking() {
    _mockMillis = 0;
    DallasTemperature::setMockTemperature(27.0f);
    tempSetup();

    // Do a full cycle first
    _mockMillis = TEMP_READ_INTERVAL;
    tempLoop();
    _mockMillis += 900;
    tempLoop();
    TEST_ASSERT_FALSE(isnan(tempRead()));

    // Call loop again immediately - should not start new request
    _mockMillis += 100;  // Still not enough for next interval
    DallasTemperature::setMockTemperature(30.0f);
    tempLoop();
    // still 27.0
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 27.0f, tempRead());
}

// --- 4. Retry on read failure (-127.0 = no sensor) ---

void test_temp_retry_on_fail() {
    _mockMillis = 0;
    DallasTemperature::setMockTemperature(-127.0f);
    tempSetup();

    // First cycle
    _mockMillis = TEMP_READ_INTERVAL;
    tempLoop();
    _mockMillis += 900;
    tempLoop();
    // Failed but retried (stays in TEMP_WAITING, requests again)
    TEST_ASSERT_TRUE_MESSAGE(isnan(tempRead()), "Temperature should be NaN after failed read");

    // Complete second cycle with valid temp
    _mockMillis += 900;
    DallasTemperature::setMockTemperature(25.5f);
    tempLoop();  // re-request happened, now waiting again

    _mockMillis += 900;
    tempLoop();  // should succeed now
    TEST_ASSERT_FALSE_MESSAGE(isnan(tempRead()), "Temperature should recover after retry");
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 25.5f, tempRead());
}

// --- 5. Max retry exhaustion ---

void test_temp_max_retry_exhaustion() {
    _mockMillis = 0;
    DallasTemperature::setMockTemperature(-127.0f);
    tempSetup();

    // Run through multiple retry cycles (MAX_RETRIES = 3)
    for (int cycle = 0; cycle < 4; cycle++) {
        _mockMillis = TEMP_READ_INTERVAL + (cycle * 2000);
        tempLoop();                   // request
        _mockMillis += 900;
        tempLoop();                   // read (fails, retries if < MAX_RETRIES)
    }

    // Should be NaN after exhausting retries
    TEST_ASSERT_TRUE_MESSAGE(isnan(tempRead()), "Temperature should be NaN after max retries");
}

// --- 6. Read returns cached value between loops ---

void test_temp_read_returns_cached() {
    _mockMillis = 0;
    DallasTemperature::setMockTemperature(30.0f);
    tempSetup();

    // Complete a cycle
    _mockMillis = TEMP_READ_INTERVAL;
    tempLoop();
    _mockMillis += 900;
    tempLoop();
    float t1 = tempRead();

    // Call tempRead multiple times without loop - should return same value
    TEST_ASSERT_EQUAL_FLOAT(t1, tempRead());
    TEST_ASSERT_EQUAL_FLOAT(t1, tempRead());
}

// ==================== Runner ====================

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_temp_initial_nan);
    RUN_TEST(test_temp_normal_read);
    RUN_TEST(test_temp_nonblocking);
    RUN_TEST(test_temp_retry_on_fail);
    RUN_TEST(test_temp_max_retry_exhaustion);
    RUN_TEST(test_temp_read_returns_cached);

    return UNITY_END();
}
