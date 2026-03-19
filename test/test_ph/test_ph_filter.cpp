#include <unity.h>
#include <ArduinoFake.h>
#include <phSensor.h>

using namespace fakeit;

// Mock variables to control time and ADC in tests
unsigned long simulated_millis = 0;
int simulated_adc = 2048;

void setUp(void) {
    ArduinoFakeReset();
    simulated_millis = 0;
    simulated_adc = 2048;
    
    // Default mocks
    When(Method(ArduinoFake(), millis)).AlwaysDo([]() { return simulated_millis; });
    When(Method(ArduinoFake(), analogRead)).AlwaysDo([](uint8_t pin) { return simulated_adc; });
    When(Method(ArduinoFake(), pinMode)).AlwaysReturn();
    When(Method(ArduinoFake(), analogSetAttenuation)).AlwaysReturn();
}

void tearDown(void) {
}

void test_ph_initialization() {
    phSetup();
    TEST_ASSERT_FALSE(phIsReady());
    TEST_ASSERT_EQUAL_FLOAT(-1.0, phRead());
}

void test_ph_filter_warmup() {
    phSetup();
    
    // We need PH_SAMPLE_COUNT samples to be ready
    // Each sample is read every PH_READ_INTERVAL ms
    for (int i = 0; i < PH_SAMPLE_COUNT; i++) {
        TEST_ASSERT_FALSE(phIsReady());
        phLoop();
        simulated_millis += PH_READ_INTERVAL;
    }
    
    // After PH_SAMPLE_COUNT samples, it should be ready
    phLoop(); 
    TEST_ASSERT_TRUE(phIsReady());
    
    // With ADC 2048 (default), pH should be around 7.0
    TEST_ASSERT_FLOAT_WITHIN(0.1, 7.0, phRead());
}

void test_ph_ema_filter_smoothing() {
    phSetup();
    
    // 1. Warm up with stable value (pH 7.0)
    for (int i = 0; i <= PH_SAMPLE_COUNT; i++) {
        phLoop();
        simulated_millis += PH_READ_INTERVAL;
    }
    float initial_ph = phRead();
    TEST_ASSERT_FLOAT_WITHIN(0.1, 7.0, initial_ph);
    
    // 2. Sudden jump in ADC (simulate noise or change)
    // Let's say it jumps to a value representing pH 8.0
    // Based on formula in phSensor.cpp: ph = 7.0 + ((voltage - voltageAt7) / slope)
    // slope is roughly -59.16 mV/pH. To increase pH by 1.0, voltage must decrease by 59.16 mV.
    // 59.16 mV is about 73 ADC units (59.16 / 3300 * 4095)
    simulated_adc = 2048 - 73; 
    
    // Read one cycle
    phLoop();
    float ph_after_1_step = phRead();
    
    // EMA Alpha is 0.15. 
    // New Value = (Raw * 0.15) + (Old * 0.85)
    // If Raw is 8.0 and Old is 7.0: (8.0 * 0.15) + (7.0 * 0.85) = 1.2 + 5.95 = 7.15
    TEST_ASSERT_FLOAT_WITHIN(0.05, 7.15, ph_after_1_step);
    
    // After many steps, it should reach ~8.0
    for (int i = 0; i < 50; i++) {
        phLoop();
        simulated_millis += PH_READ_INTERVAL;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1, 8.0, phRead());
}

void test_ph_nan_handling() {
    phSetup();
    
    // Warm up
    for (int i = 0; i <= PH_SAMPLE_COUNT; i++) {
        phLoop();
        simulated_millis += PH_READ_INTERVAL;
    }
    
    // Simulate sensor disconnect (ADC = 0)
    simulated_adc = 0;
    phLoop();
    
    // Should return NAN
    TEST_ASSERT_TRUE(isnan(phRead()));
    
    // Restore and see if it recovers
    simulated_adc = 2048;
    phLoop();
    TEST_ASSERT_FALSE(isnan(phRead()));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ph_initialization);
    RUN_TEST(test_ph_filter_warmup);
    RUN_TEST(test_ph_ema_filter_smoothing);
    RUN_TEST(test_ph_nan_handling);
    return UNITY_END();
}
