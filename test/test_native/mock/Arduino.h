/**
 * @file Arduino.h
 * @brief Minimal Arduino.h mock for native unit testing
 * Declarations only — test files provide definitions.
 */

#ifndef MOCK_ARDUINO_H_SYS
#define MOCK_ARDUINO_H_SYS

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>

// Basic types
#include <stdint.h>
typedef uint8_t byte;
typedef bool boolean;
typedef uint8_t uint8_t_std;
using std::isnan;

// GPIO
#define INPUT 0x01
#define OUTPUT 0x02
#define INPUT_PULLUP 0x05
#define HIGH 1
#define LOW 0
#define LED_BUILTIN 48

// ADC
#define ADC_11db 3

// F() macro
#ifndef F
#define F(str) (str)
#endif

#endif
