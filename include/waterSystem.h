/**
 * @file waterSystem.h
 * @brief Water circulation and refill controller
 */

#ifndef WATER_SYSTEM_H
#define WATER_SYSTEM_H

#include <Arduino.h>

typedef enum {
    WATER_STATE_IDLE = 0,
    WATER_STATE_MIX_TANK_REFILL,
    WATER_STATE_WAIT_REFILL_INTERVAL,
    WATER_STATE_MIX_TANK_SETTLING,
    WATER_STATE_FISH_TANK_REFILL,
    WATER_STATE_BLOCKED,
    WATER_STATE_ALARM
} WaterSystemState;

typedef enum {
    WATER_REFILL_ROUTE_AUTO = 0,
    WATER_REFILL_ROUTE_FISH_TANK,
    WATER_REFILL_ROUTE_SUMP_DIRECT,
    WATER_REFILL_ROUTE_NONE
} WaterRefillRoute;

typedef struct {
    bool circulationEnabled;
    bool refillEnabled;
    bool manualRefill;
    unsigned long refillMaxRuntimeMs;
    unsigned long refillMinIntervalMs;
    WaterRefillRoute preferredRoute;
    bool allowDirectSumpRefill;
    unsigned long fishRefillIntervalMs;
    unsigned long fishRefillMaxRuntimeMs;
} WaterSystemConfig;

typedef struct {
    WaterSystemState state;
    WaterRefillRoute activeRoute;
    bool circulationOutput;
    bool refillOutput;
    bool routeValveOutput;
    bool circulationPumpOutput;
    bool fishTankRefillOutput;
    bool mixTankRefillOutput;
    bool waterDilutionActive;
    bool mixTankSettlingActive;
    bool mixTankControlZone;
    unsigned long dilutionHoldRemainingMs;
    bool fishRefillReady;
    unsigned long fishRefillWaitRemainingMs;
    bool levelLow;
    bool levelHigh;
    bool overflowAlarm;
    bool hasCirculationPump;
    bool hasRefillPump;
    bool hasLevelSensors;
    bool hasOverflowSensor;
    bool hasRouteValve;
    bool routeBlocked;
    bool alarmActive;
    char reason[256];
} WaterSystemStatus;

void waterSystemSetup(void);
void waterSystemLoop(void);
void waterSystemSetConfig(bool circulationEnabled,
                          bool refillEnabled,
                          unsigned long refillMaxRuntimeMs,
                          unsigned long refillMinIntervalMs,
                          WaterRefillRoute preferredRoute,
                          bool allowDirectSumpRefill,
                          unsigned long fishRefillIntervalMs,
                          unsigned long fishRefillMaxRuntimeMs);
void waterSystemGetConfig(WaterSystemConfig* config);
void waterSystemGetStatus(WaterSystemStatus* status);
const char* waterSystemGetStateString(WaterSystemState state);
const char* waterSystemGetStateLabelTh(WaterSystemState state);
const char* waterSystemGetRouteString(WaterRefillRoute route);
void waterSystemSetManualRefill(bool enabled);
void waterSystemSetCirculationEnabled(bool enabled);
void waterSystemSetPreferredRoute(WaterRefillRoute route);
void waterSystemSetAllowDirectSumpRefill(bool enabled);
void waterSystemClearAlarm(void);

#endif // WATER_SYSTEM_H