/**
 * @file Wire.h
 * @brief Mock Wire (I2C) library for native unit testing
 */

#ifndef MOCK_WIRE_H
#define MOCK_WIRE_H

#include <cstdint>

class TwoWire {
public:
    void begin(int, int) {}
};
extern TwoWire Wire;

#endif // MOCK_WIRE_H
