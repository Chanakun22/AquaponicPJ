/**
 * @file test_dht_native.cpp
 * @brief Full unit test suite for DHT22 Sensor
 *
 * Tests: initialization, temperature/humidity read, loop timing,
 * NaN handling on read failure, cached values, interval respect
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
#define DHT_PIN             15
#define DHT_TYPE            DHT22
#define DHT_READ_INTERVAL   2000
#define I2C_SDA_PIN         8
#define I2C_SCL_PIN         9
#define ONE_WIRE_PIN        13

// ========== String stub (needed by some Serial.print macros) ==========
class String {
public:
    String() {}
    String(const char*) {}
    operator const char*() const { return ""; }
};

// ========== F() macro ==========
#define F(str) (str)

// ========== FP helpers ==========
#ifndef isnan
#define isnan(x) std::isnan(x)
#endif

// ========== Include mock DHT before source ==========
#include "mock/DHT.h"

// ========== Static mock definitions ==========
float DHT::_mockTemperature = 25.0f;
float DHT::_mockHumidity = 60.0f;

// ========== Include real DHT source ==========
#include "../../src/dhtSensor.cpp"

// ========== Test Cases ==========

// --- 1. Initial values are valid after setup ---

void test_dht_initial_values_valid() {
    _mockMillis = 0;
    DHT::setMockTemperature(25.0f);
    DHT::setMockHumidity(60.0f);
    dhtSetup();
    dhtLoop();

    float t = dhtReadTemperature();
    float h = dhtReadHumidity();

    TEST_ASSERT_FALSE_MESSAGE(isnan(t), "Temperature should be valid after setup");
    TEST_ASSERT_FALSE_MESSAGE(isnan(h), "Humidity should be valid after setup");
    TEST_ASSERT_EQUAL_FLOAT(25.0f, t);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, h);
}

// --- 2. dhtLoop reads at correct interval ---

void test_dht_loop_reads_at_interval() {
    _mockMillis = 0;
    DHT::setMockTemperature(26.5f);
    DHT::setMockHumidity(55.0f);
    dhtSetup();

    // Immediately call loop -> should NOT read (interval not elapsed)
    dhtLoop();
    TEST_ASSERT_EQUAL_FLOAT(26.5f, dhtReadTemperature());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, dhtReadHumidity());

    // Advance time to exactly interval
    _mockMillis = DHT_READ_INTERVAL;
    DHT::setMockTemperature(27.0f);
    DHT::setMockHumidity(50.0f);
    dhtLoop();

    TEST_ASSERT_EQUAL_FLOAT(27.0f, dhtReadTemperature());
    TEST_ASSERT_EQUAL_FLOAT(50.0f, dhtReadHumidity());
}

// --- 3. Cache returns NAN when sensor read fails ---

void test_dht_nan_on_failed_read() {
    _mockMillis = 0;
    DHT::setMockTemperature(NAN);
    DHT::setMockHumidity(NAN);
    dhtSetup();

    _mockMillis = DHT_READ_INTERVAL;
    dhtLoop();

    TEST_ASSERT_TRUE_MESSAGE(isnan(dhtReadTemperature()), "Temperature should be NaN on failed read");
    TEST_ASSERT_TRUE_MESSAGE(isnan(dhtReadHumidity()), "Humidity should be NaN on failed read");
}

// --- 4. Temperature and humidity vary independently ---

void test_dht_independent_readings() {
    _mockMillis = 0;
    DHT::setMockTemperature(30.0f);
    DHT::setMockHumidity(70.0f);
    dhtSetup();

    _mockMillis = DHT_READ_INTERVAL;
    DHT::setMockTemperature(35.0f);
    DHT::setMockHumidity(70.0f);  // humidity unchanged
    dhtLoop();

    TEST_ASSERT_EQUAL_FLOAT(35.0f, dhtReadTemperature());
    TEST_ASSERT_EQUAL_FLOAT(70.0f, dhtReadHumidity());
}

// --- 5. Non-blocking: no read if interval not elapsed ---

void test_dht_nonblocking_no_read() {
    _mockMillis = 0;
    DHT::setMockTemperature(25.0f);
    DHT::setMockHumidity(60.0f);
    dhtSetup();

    // Do a full read cycle
    _mockMillis = DHT_READ_INTERVAL;
    DHT::setMockTemperature(26.0f);
    dhtLoop();
    TEST_ASSERT_EQUAL_FLOAT(26.0f, dhtReadTemperature());

    // Try to read immediately without advancing time
    DHT::setMockTemperature(30.0f);
    dhtLoop();
    // Should NOT have updated because interval not elapsed
    TEST_ASSERT_EQUAL_FLOAT(26.0f, dhtReadTemperature());
}

// --- 6. Recovery from NaN ---

void test_dht_recovery_from_nan() {
    _mockMillis = 0;
    DHT::setMockTemperature(NAN);
    DHT::setMockHumidity(NAN);
    dhtSetup();

    // Failed read
    _mockMillis = DHT_READ_INTERVAL;
    dhtLoop();
    TEST_ASSERT_TRUE(isnan(dhtReadTemperature()));

    // Valid read comes back
    DHT::setMockTemperature(28.0f);
    DHT::setMockHumidity(65.0f);
    _mockMillis = DHT_READ_INTERVAL * 2;
    dhtLoop();

    TEST_ASSERT_EQUAL_FLOAT(28.0f, dhtReadTemperature());
    TEST_ASSERT_EQUAL_FLOAT(65.0f, dhtReadHumidity());
}

// ==================== Runner ====================

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_dht_initial_values_valid);
    RUN_TEST(test_dht_loop_reads_at_interval);
    RUN_TEST(test_dht_nan_on_failed_read);
    RUN_TEST(test_dht_independent_readings);
    RUN_TEST(test_dht_nonblocking_no_read);
    RUN_TEST(test_dht_recovery_from_nan);

    return UNITY_END();
}
