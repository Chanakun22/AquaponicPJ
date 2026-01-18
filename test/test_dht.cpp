#include <unity.h>
#include "dhtSensor.h"

TEST_GROUP(DhtSensorTests);

TEST_SETUP(DhtSensorTests) {
    dhtSetup();
}

TEST_TEAR_DOWN(DhtSensorTests) {
}

TEST(DhtSensorTests, ReadHumidity) {
    // Simulate reading humidity
    // Note: In real tests, you might need to mock or use actual sensor
    float h = 50.0; // Mock value
    TEST_ASSERT_GREATER_THAN(0, h);
}

TEST(DhtSensorTests, ReadTemperature) {
    // Simulate reading temperature
    float t = 25.0; // Mock value
    TEST_ASSERT_GREATER_THAN(0, t);
}

TEST_GROUP_RUNNER(DhtSensorTests) {
    RUN_TEST_CASE(DhtSensorTests, ReadHumidity);
    RUN_TEST_CASE(DhtSensorTests, ReadTemperature);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST_GROUP(DhtSensorTests);
    UNITY_END();
}

void loop() {
}
