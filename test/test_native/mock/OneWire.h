/**
 * @file OneWire.h
 * @brief Mock OneWire library for native unit testing
 */

#ifndef MOCK_ONEWIRE_H
#define MOCK_ONEWIRE_H

#include <cstdint>

class OneWire {
public:
    OneWire(uint8_t pin) {}
    void begin() {}
};

#endif // MOCK_ONEWIRE_H
