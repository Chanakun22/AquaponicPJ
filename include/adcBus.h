/**
 * @file adcBus.h
 * @brief Shared ESP32 ADC settle + oversampled read สำหรับ TDS / pH (ลด crosstalk ระหว่าง pin)
 */

#ifndef ADC_BUS_H
#define ADC_BUS_H

#include <Arduino.h>
#include "config.h"

#ifndef ADC_CHANNEL_SWITCH_SETTLE_US
#define ADC_CHANNEL_SWITCH_SETTLE_US 400
#endif

#ifndef ADC_SAMPLE_SETTLE_US
#define ADC_SAMPLE_SETTLE_US 250
#endif

static uint8_t _adcBusActivePin = 0xFF;
static bool _adcBusInitialized = false;

inline void adcBusSetup(void) {
#if defined(ESP32)
    if (!_adcBusInitialized) {
        analogReadResolution(12);
        analogSetAttenuation(ADC_11db);
        _adcBusInitialized = true;
    }
#endif
    (void)_adcBusInitialized;
}

inline void adcBusSettlePin(uint8_t pin) {
    if (_adcBusActivePin != pin) {
        delayMicroseconds(ADC_CHANNEL_SWITCH_SETTLE_US);
        _adcBusActivePin = pin;
    }
}

inline void adcBusResetForTest(void) {
    _adcBusActivePin = 0xFF;
    _adcBusInitialized = false;
}

inline int adcBusReadOversampled(uint8_t pin,
                                 int dummyReads,
                                 int oversampleCount,
                                 int settleUs) {
    adcBusSetup();
    adcBusSettlePin(pin);

    long sum = 0;
    for (int i = 0; i < dummyReads; i++) {
        delayMicroseconds(settleUs);
        (void)analogRead(pin);
    }
    for (int i = 0; i < oversampleCount; i++) {
        delayMicroseconds(settleUs);
        sum += analogRead(pin);
    }
    return static_cast<int>(sum / oversampleCount);
}

#endif // ADC_BUS_H
