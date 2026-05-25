#include <cstddef>
#include <cstdint>

#include "../test_native/mock/Arduino.h"
#include "config.h"
#include "gpioOut.h"

// Forward declaration of mocked Arduino API (defined inside the test file).
void digitalWrite(uint8_t pin, uint8_t value);

size_t telnetPrintfNonBlocking(const char*, ...) { return 0; }
void localMqttPublishLog(const char*) {}

// === Mock gpioOut for native tests ===
// Map logical outputs to the same ESP32 GPIO pins as production config.h,
// then forward to the digitalWrite mock so existing pin-state assertions still pass.

static int _logicalOutputToPin(GpioLogicalOutput out) {
    switch (out) {
        case GPIO_OUT_PUMP_NUTRIENT_A:    return PUMP_NUTRIENT_A_PIN;
        case GPIO_OUT_PUMP_NUTRIENT_B:    return PUMP_NUTRIENT_B_PIN;
        case GPIO_OUT_LIGHT_RELAY:        return LIGHT_RELAY_PIN;
        case GPIO_OUT_PUMP_CIRCULATION:   return PUMP_CIRCULATION_PIN;
        case GPIO_OUT_FISH_FEEDER:        return FISH_FEEDER_PIN;
        case GPIO_OUT_REFILL_ROUTE_VALVE: return REFILL_ROUTE_VALVE_PIN;
        case GPIO_OUT_PUMP_REFILL:        return PUMP_REFILL_PIN;
        case GPIO_OUT_EXHAUST_FAN:        return EXHAUST_FAN_PIN;
        default:                          return -1;
    }
}

bool gpioOutSetup(void) { return true; }

void gpioOutWrite(GpioLogicalOutput out, bool on) {
    int pin = _logicalOutputToPin(out);
    if (pin >= 0) {
        digitalWrite((uint8_t)pin, on ? PUMP_ON : PUMP_OFF);
    }
}

bool gpioOutGetLastState(GpioLogicalOutput) { return false; }
bool gpioOutMcpHealthy(void) { return true; }
bool gpioOutMcpReinit(void) { return true; }
int gpioOutGetEsp32Pin(GpioLogicalOutput out) { return _logicalOutputToPin(out); }
int gpioOutGetMcpPin(GpioLogicalOutput) { return -1; }

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

#include "../test_native/test_water_system_native.cpp"
