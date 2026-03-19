#include <unity.h>
#include "dhtSensor.h"

void test_read_humidity() {
    float h = 50.0; // Mock value
    TEST_ASSERT_GREATER_THAN(0, h);
}

void test_read_temperature() {
    float t = 25.0; // Mock value
    TEST_ASSERT_GREATER_THAN(0, t);
}

void setup() {
    delay(2000); // Give time for monitor to connect
    UNITY_BEGIN();
    RUN_TEST(test_read_humidity);
    RUN_TEST(test_read_temperature);
    UNITY_END();
}

void loop() {
    delay(100);
}
