/**
 * @file fanController.h
 * @brief Exhaust fan controller for air temperature/humidity management
 */

#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include <Arduino.h>

typedef enum {
    FAN_STATE_DISABLED = 0,
    FAN_STATE_IDLE,
    FAN_STATE_RUNNING,
    FAN_STATE_BLOCKED
} FanControlState;

typedef struct {
    bool enabled;
    bool autoMode;
    bool manualState;
    float tempOnC;
    float tempOffC;
    float humidityOnPct;
    float humidityOffPct;
} FanControlConfig;

typedef struct {
    FanControlState state;
    bool running;
    bool hasOutput;
    float airTempC;
    float humidityPct;
    char reason[96];
} FanControlStatus;

void fanCtrlSetup(void);
void fanCtrlLoop(void);
void fanCtrlSetConfig(bool enabled,
                      bool autoMode,
                      bool manualState,
                      float tempOnC,
                      float tempOffC,
                      float humidityOnPct,
                      float humidityOffPct);
void fanCtrlGetConfig(FanControlConfig* config);
void fanCtrlGetStatus(FanControlStatus* status);
void fanCtrlSetEnabled(bool enabled);
void fanCtrlSetAutoMode(bool enabled);
void fanCtrlSetManualState(bool enabled);
bool fanCtrlGetState(void);
const char* fanCtrlGetStateString(FanControlState state);
const char* fanCtrlGetModeString(bool autoMode);

#endif // FAN_CONTROLLER_H