/**
 * @file fishFeeder.h
 * @brief Fish feeder controller with schedule and selectable command source
 */

#ifndef FISH_FEEDER_H
#define FISH_FEEDER_H

#include <Arduino.h>
#include "controlSource.h"

typedef enum {
    FEEDER_STATE_DISABLED = 0,
    FEEDER_STATE_IDLE,
    FEEDER_STATE_FEEDING,
    FEEDER_STATE_BLOCKED
} FishFeederState;

typedef struct {
    CommandSource commandSource;
    bool enabled;
    int feedDay;
    char feedTime[6];
    unsigned long durationMs;
} FishFeederConfig;

typedef struct {
    FishFeederState state;
    bool running;
    bool hasOutput;
    char lastFeedAt[24];
    char reason[96];
} FishFeederStatus;

void fishFeederSetup(void);
void fishFeederLoop(void);
void fishFeederGetConfig(FishFeederConfig* config);
void fishFeederGetStatus(FishFeederStatus* status);
void fishFeederSetCommandSource(CommandSource source);
CommandSource fishFeederGetCommandSource(void);
bool fishFeederAllowsNetpieControl(void);
bool fishFeederAllowsLocalControl(void);
void fishFeederSetEnabled(bool enabled);
void fishFeederSetFeedDay(int day);
void fishFeederSetFeedTime(const char* timeStr);
void fishFeederSetDurationMs(unsigned long durationMs);
bool fishFeederStartManualFeed(const char* reason);
const char* fishFeederGetStateString(FishFeederState state);

#endif