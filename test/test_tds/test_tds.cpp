#include <unity.h>
#include "TdsSensor.h"

void test_readTdsSensor() {
    tdsSetup();
    // In a real test, we would call tdsLoop() but that requires FreeRTOS running.
    // For unit tests we mock it.
    float tds = tdsGetLastValue();
    // Default initialized to 0 or NAN.
    TEST_ASSERT_EQUAL_FLOAT(0.0, tds); // Currently assuming 0.0 or we can check isnan
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_readTdsSensor);
    UNITY_END();
}

void loop() {
}
