/**
 * @file DallasTemperature.h
 * @brief Mock DallasTemperature library for native unit testing
 */

#ifndef MOCK_DALLASTEMPERATURE_H
#define MOCK_DALLASTEMPERATURE_H

#include <cstdint>

class OneWire;

class DallasTemperature {
public:
    DallasTemperature(OneWire*) {}
    void begin() {}
    void setWaitForConversion(bool) {}
    int getDeviceCount() { return 1; }
    void requestTemperatures() {}
    float getTempCByIndex(int) { return _mockTemp; }
    
    static float _mockTemp;
    static void setMockTemperature(float t) { _mockTemp = t; }
};

#endif // MOCK_DALLASTEMPERATURE_H
