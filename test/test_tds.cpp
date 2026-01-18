#include <unity.h>
#include "TdsSensor.h"

void test_readTdsSensor() {
    setupTdsSensor();
    float tds = readTdsSensor(25.0);
    TEST_ASSERT_GREATER_THAN(0, tds);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_readTdsSensor);
    UNITY_END();
}

void loop() {
}
