/**
 * @file mock_arduino.cpp
 * @brief Implementation of Arduino/ESP32 mock for native unit testing
 */

#include "mock_arduino.h"

// ==================== Global Mock State ====================

unsigned long _mockMillis = 0;
int _mockAnalogValues[40] = {0};

// ==================== Time ====================

unsigned long millis() { return _mockMillis; }

unsigned long micros() {
    return _mockMillis * 1000UL;
}

void delay(unsigned long ms) {
    _mockMillis += ms;
}

void delayMicroseconds(unsigned int us) {
    _mockMillis += (us > 0) ? 1 : 0;
}

// ==================== ADC ====================

int analogRead(uint8_t pin) {
    if (pin < 40)
        return _mockAnalogValues[pin];
    return 0;
}

// ==================== GPIO ====================

static int _mockPinModes[40] = {0};
static int _mockPinStates[40] = {0};

void pinMode(uint8_t pin, uint8_t mode) {
    if (pin < 40) _mockPinModes[pin] = mode;
}

void digitalWrite(uint8_t pin, uint8_t val) {
    if (pin < 40) _mockPinStates[pin] = val;
}

int digitalRead(uint8_t pin) {
    if (pin < 40) return _mockPinStates[pin];
    return 0;
}

void analogSetAttenuation(int) { /* no-op */ }

// ==================== Serial ====================

MockSerial Serial;

// ==================== FreeRTOS port macros for TDS/ESP32 ====================
// On native, we are single-threaded, so these become no-ops

#ifndef portMUX_TYPE
#define portMUX_TYPE int
#endif

#ifndef portMUX_INITIALIZER_UNLOCKED
#define portMUX_INITIALIZER_UNLOCKED 0
#endif

#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL(mux) (void)(mux)
#define portEXIT_CRITICAL(mux)  (void)(mux)
#endif
