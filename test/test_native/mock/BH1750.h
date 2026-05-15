/**
 * @file BH1750.h
 * @brief Mock BH1750 light sensor library for native unit testing
 */

#ifndef MOCK_BH1750_H
#define MOCK_BH1750_H

#include <cstdint>

class BH1750 {
public:
    enum Mode {
        CONTINUOUS_HIGH_RES_MODE
    };
    
    BH1750() {}
    bool begin(Mode, uint8_t) { return _mockBeginResult; }
    bool measurementReady() { return true; }
    float readLightLevel() { return _mockLux; }
    
    static bool _mockBeginResult;
    static float _mockLux;
    
    static void setMockBeginResult(bool r) { _mockBeginResult = r; }
    static void setMockLux(float l) { _mockLux = l; }
};

#endif // MOCK_BH1750_H
