/**
 * @file automator.h
 * @brief State Machine for Aquaponics Automation
 */

#ifndef AUTOMATOR_H
#define AUTOMATOR_H

#include <Arduino.h>

/**
 * @brief สถานะการทำงานของระบบควบคุมอัตโนมัติ (Process State)
 */
enum AutomatorState {
    AUTO_STATE_DISABLED,    // ระบบถูกปิดใช้งาน
    AUTO_STATE_IDLE,        // รอตรวจสอบเซ็นเซอร์
    AUTO_STATE_EVALUATING,  // ตรวจพบค่าผิดปกติ กำลังประเมินผล
    AUTO_STATE_DOSING_A,    // กำลังจ่ายปุ๋ย A
    AUTO_STATE_DOSING_B,    // กำลังจ่ายปุ๋ย B
    AUTO_STATE_WATER_FILL,  // กำลังเติมน้ำ
    AUTO_STATE_COOLDOWN     // พักระบบเพื่อรอสารละลายเข้ากัน
};

// Configuration Struct
struct AutomatorConfig {
    bool enabled;
    float targetTds;
    float targetPh;
};

// Public Functions
void automatorSetup(void);
void automatorLoop(void);

// NVS Settings Management
void automatorSetConfig(bool enabled, float targetTds, float targetPh);
void automatorGetConfig(AutomatorConfig* config);

// Get current state for MQTT
AutomatorState automatorGetCurrentState(void);
const char* automatorGetStateString(AutomatorState state);
const char* automatorGetActionReason(void);
const char* automatorGetNextStateString(void);
int automatorGetTimeRemainingSec(void);

// HW Test: pause/resume automator to prevent interference 
void automatorPause(void);
void automatorResume(void);

#endif // AUTOMATOR_H
