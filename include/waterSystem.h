/**
 * @file waterSystem.h
 * @brief Water circulation and refill controller
 */

#ifndef WATER_SYSTEM_H
#define WATER_SYSTEM_H

#include <Arduino.h>

typedef enum {
    WATER_STATE_DISABLED = 0,
    WATER_STATE_CIRCULATION,
    WATER_STATE_REFILLING,
    WATER_STATE_BLOCKED,
    WATER_STATE_ALARM
} WaterSystemState;

typedef struct {
    bool circulationEnabled;
    bool refillEnabled;
    bool manualRefill;
    unsigned long refillMaxRuntimeMs;
} WaterSystemConfig;

typedef struct {
    WaterSystemState state;
    bool circulationOutput;
    bool refillOutput;
    bool levelLow;
    bool levelHigh;
    bool overflowAlarm;
    bool hasCirculationPump;
    bool hasRefillPump;
    bool hasLevelSensors;
    bool hasOverflowSensor;
    bool alarmActive;
    char reason[96];
} WaterSystemStatus;

void waterSystemSetup(void);
void waterSystemLoop(void);
void waterSystemSetConfig(bool circulationEnabled, bool refillEnabled, unsigned long refillMaxRuntimeMs);
void waterSystemGetConfig(WaterSystemConfig* config);
void waterSystemGetStatus(WaterSystemStatus* status);
const char* waterSystemGetStateString(WaterSystemState state);
void waterSystemSetManualRefill(bool enabled);
void waterSystemSetCirculationEnabled(bool enabled);
void waterSystemClearAlarm(void);

#endif // WATER_SYSTEM_H