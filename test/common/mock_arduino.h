/**
 * @file mock_arduino.h
 * @brief Minimal Arduino/ESP32 mock layer for native (PC) unit testing
 *
 * ครอบคลุมฟังก์ชันที่เซ็นเซอร์ใช้: analogRead, millis, pinMode, Serial, delay, ฯลฯ
 * ใช้คู่กับ mock_preferences.h และ mock_esp32.h
 */

#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <cstdint>
#include <cstddef>
#include <cmath>

// ==================== Time ====================

extern unsigned long _mockMillis;
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

// ==================== ADC ====================

extern int _mockAnalogValues[40]; // one per pin
int analogRead(uint8_t pin);

// ==================== GPIO ====================

#define INPUT 0x01
#define OUTPUT 0x02
#define INPUT_PULLUP 0x05

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);

// ==================== ESP32 ADC attenuation ====================

#define ADC_11db 3
void analogSetAttenuation(int att);

// ==================== Serial stub ====================

struct MockSerial {
    void begin(unsigned long) {}
    void print(const char*) {}
    void print(char) {}
    void print(int) {}
    void print(float, int = 2) {}
    void println(const char*) {}
    void println(char) {}
    void println(int) {}
    void println(float, int = 2) {}
    void println() {}
    void printf(const char*, ...) {}
};
extern MockSerial Serial;

// ==================== String (Arduino) ====================

#include <string>

class String {
public:
    std::string _s;
    String() {}
    String(const char* s) : _s(s) {}
    String(const std::string& s) : _s(s) {}
    const char* c_str() const { return _s.c_str(); }
    int length() const { return (int)_s.length(); }
    char charAt(int i) const { return _s[i]; }
    bool startsWith(const char* prefix) const {
        return _s.rfind(prefix, 0) == 0;
    }
    bool equals(const char* other) const { return _s == other; }
    int indexOf(char c) const {
        auto pos = _s.find(c);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    String substring(int start, int end = -1) const {
        if (end < 0) return _s.substr(start);
        return _s.substr(start, end - start);
    }
    int toInt() const { return std::stoi(_s); }
    float toFloat() const { return std::stof(_s); }
};

// ==================== FP Classification ====================

#ifndef isnan
#define isnan(x) std::isnan(x)
#endif

#endif // MOCK_ARDUINO_H