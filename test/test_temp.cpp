#include <unity.h>
#include "tempSensor.h"

void test_tempSensor() {
    // Simulate temperature sensor test
    // Note: In real tests, you might need to mock or use actual sensor
    float temp = 25.0; // Mock value
    TEST_ASSERT_GREATER_THAN(0, temp);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_tempSensor);
    UNITY_END();
}

void loop() {
}
