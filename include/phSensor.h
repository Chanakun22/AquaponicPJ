/**
 * @file phSensor.h
 * @brief อ่านค่า pH จาก Analog pH Sensor (E-201-C)
 * @details รองรับ 2 channel (mix tank + fish tank) และ calibration 3 จุด (pH 4.01, 6.86, 9.18) ต่อ channel
 */

#ifndef PH_SENSOR_H
#define PH_SENSOR_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define PH_SAMPLE_COUNT     30      // จำนวน sample สำหรับ averaging
#define PH_READ_INTERVAL    1000    // อ่านค่าทุก (ms)
#define PH_OVERSAMPLE_COUNT 16      // จำนวน analogRead ต่อ 1 รอบ sampling
#define PH_INVALID_STREAK_LIMIT 3   // ต้อง invalid ติดต่อกันกี่รอบจึง mark ว่าหลุดช่วง
#define PH_VOLTAGE_FILTER_ALPHA 0.18f // Processing EMA for pH calculation: faster but still damped
#define PH_VOLTAGE_DISPLAY_FILTER_ALPHA 0.06f // Extra smoothing just for displayed voltage (mV)
#define PH_PH_FILTER_ALPHA    0.18f // Balanced pH EMA: quicker response without going fully raw
#define PH_VOLTAGE_DEADBAND_MV 3.0f // Smaller process deadband so pH value moves sooner
#define PH_VOLTAGE_DISPLAY_DEADBAND_MV 8.0f // Larger display deadband so calibration voltage looks steadier
#define PH_PH_DEADBAND        0.01f // Allow smaller visible pH movement before holding value
#define PH_PH_MAX_STEP        0.12f // Let pH catch up faster per cycle while still limiting jumps
#define PH_ADC_SETTLE_US      ADC_SAMPLE_SETTLE_US
#define PH_CHANNEL_SWITCH_SETTLE_US ADC_CHANNEL_SWITCH_SETTLE_US
#define PH_ADC_DUMMY_READS    2     // Discard initial ADC reads so pH sampling starts after channel settles

// Calibration values (ต้อง calibrate ใหม่ตาม sensor จริง)
#define PH_VOLTAGE_AT_686   2058    // ADC value ที่ pH 6.86 (neutral-ish) - default reference
#define PH_VOLTAGE_SLOPE    -59.16  // mV per pH unit at 25°C

// ============================================================================
// CHANNEL DEFINITIONS
// ============================================================================

typedef enum {
    PH_CHANNEL_MIX = 0,     // ถังผสม (probe เดิม, GPIO 6)
    PH_CHANNEL_FISH,        // ตู้ปลา (probe ใหม่, GPIO 1)
    PH_CHANNEL_COUNT
} PhChannel;

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES (multi-channel)
// ============================================================================

void phSetup(void);
void phLoop(void);

float phReadChannel(PhChannel channel);
float phReadVoltageChannel(PhChannel channel);
bool  phIsReadyChannel(PhChannel channel);

void phCalibratePh686Channel(PhChannel channel);
void phCalibratePh401Channel(PhChannel channel);
void phCalibratePh918Channel(PhChannel channel);

bool phHasCalibration401Channel(PhChannel channel);
bool phHasCalibration686Channel(PhChannel channel);
bool phHasCalibration918Channel(PhChannel channel);

void phClearCalibrationChannel(PhChannel channel);

void phSetTemperature(float temperature);   // backward compat: sets mix channel only
void phSetTemperatureChannel(PhChannel channel, float temperature);

// ============================================================================
// BACKWARD-COMPAT SHIMS — default = MIX channel
// ============================================================================

inline float phRead(void)            { return phReadChannel(PH_CHANNEL_MIX); }
inline float phReadVoltage(void)     { return phReadVoltageChannel(PH_CHANNEL_MIX); }
inline bool  phIsReady(void)         { return phIsReadyChannel(PH_CHANNEL_MIX); }
inline void  phCalibratePh686(void)  { phCalibratePh686Channel(PH_CHANNEL_MIX); }
inline void  phCalibratePh401(void)  { phCalibratePh401Channel(PH_CHANNEL_MIX); }
inline void  phCalibratePh918(void)  { phCalibratePh918Channel(PH_CHANNEL_MIX); }
inline bool  phHasCalibration401(void) { return phHasCalibration401Channel(PH_CHANNEL_MIX); }
inline bool  phHasCalibration686(void) { return phHasCalibration686Channel(PH_CHANNEL_MIX); }
inline bool  phHasCalibration918(void) { return phHasCalibration918Channel(PH_CHANNEL_MIX); }
inline void  phClearCalibration(void)  { phClearCalibrationChannel(PH_CHANNEL_MIX); }

#endif // PH_SENSOR_H
