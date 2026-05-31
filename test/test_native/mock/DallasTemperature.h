/**
 * @file DallasTemperature.h
 * @brief Mock DallasTemperature library for native unit testing
 */

#ifndef MOCK_DALLASTEMPERATURE_H
#define MOCK_DALLASTEMPERATURE_H

#include <cstdint>
#include <cstring>

typedef uint8_t DeviceAddress[8];
#define DEVICE_DISCONNECTED_C -127.0f

class OneWire;

class DallasTemperature {
public:
    DallasTemperature(OneWire*) {}
    void begin() {}
    void setWaitForConversion(bool) {}
    int getDeviceCount() { return 1; }
    void requestTemperatures() {}
    float getTempCByIndex(int) { return _mockTemp; }
    bool getAddress(DeviceAddress addr, uint8_t index) {
        if (index > 0) return false;
        const uint8_t mock[8] = {0x28, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
        std::memcpy(addr, mock, 8);
        return true;
    }
    float getTempC(const DeviceAddress) { return _mockTemp; }
    
    static float _mockTemp;
    static void setMockTemperature(float t) { _mockTemp = t; }
};

#endif // MOCK_DALLASTEMPERATURE_H
