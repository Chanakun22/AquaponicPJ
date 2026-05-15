/**
 * @file esp32-hal.h
 * @brief Minimal ESP32 HAL stub for native testing
 */

#ifndef MOCK_ESP32_HAL_H
#define MOCK_ESP32_HAL_H

#include <cstdint>

// portMUX (spinlock) — no-op on native
struct portMUX_TYPE {
    int _;
};

#define portMUX_INITIALIZER_UNLOCKED {0}

static inline void portENTER_CRITICAL(portMUX_TYPE*) {}
static inline void portEXIT_CRITICAL(portMUX_TYPE*) {}

#endif
