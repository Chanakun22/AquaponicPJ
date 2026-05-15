/**
 * @file DHT.h
 * @brief Mock DHT sensor library for native unit testing
 */

#ifndef MOCK_DHT_H
#define MOCK_DHT_H

#include <cstdint>

#define DHT22 22
#define DHT11 11
#define DHT21 21

class DHT {
private:
    uint8_t _pin;
    uint8_t _type;
    static float _mockTemperature;
    static float _mockHumidity;
public:
    DHT(uint8_t pin, uint8_t type) : _pin(pin), _type(type) {}
    void begin() {}
    float readTemperature() { return _mockTemperature; }
    float readHumidity() { return _mockHumidity; }
    
    static void setMockTemperature(float t) { _mockTemperature = t; }
    static void setMockHumidity(float h) { _mockHumidity = h; }
};

#endif // MOCK_DHT_H
