/**
 * @file i2cBus.h
 * @brief Shared I2C bus setup and locking helpers.
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <Arduino.h>

#ifdef NATIVE_TEST
inline void i2cBusSetup(void) {}
inline bool i2cBusLock(uint32_t timeoutMs = 50) {
    (void)timeoutMs;
    return true;
}
inline void i2cBusUnlock(void) {}
#else
void i2cBusSetup(void);
bool i2cBusLock(uint32_t timeoutMs = 50);
void i2cBusUnlock(void);
#endif

#endif // I2C_BUS_H
