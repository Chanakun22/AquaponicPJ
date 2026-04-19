/**
 * @file lightController.h
 * @brief ควบคุมไฟตามตารางเวลาจาก NETPIE
 * @details รับค่า lightEnabled, lightOnTime, lightOffTime, lightDays
 */

#ifndef LIGHT_CONTROLLER_H
#define LIGHT_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

typedef struct {
	CommandSource commandSource;
	bool enabled;
	bool manualState;
	int onDay;
	int offDay;
	char onTime[6];
	char offTime[6];
} LightControlConfig;

typedef struct {
	bool running;
	bool ntpSynced;
	bool hasOutput;
	char reason[96];
} LightControlStatus;

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้น Light Controller และ NTP
 */
void lightCtrlSetup(void);

/**
 * @brief ตรวจสอบตารางเวลาและควบคุมไฟ
 * @note เรียกใช้ใน loop()
 */
void lightCtrlLoop(void);

/**
 * @brief อัพเดทการตั้งค่าจาก NETPIE (แบบ partial update)
 */
void lightCtrlSetEnabled(int enabled);
void lightCtrlSetManualState(bool state);
void lightCtrlSetCommandSource(CommandSource source);
void lightCtrlSetOnDay(int day);
void lightCtrlSetOnTime(const char* onTime);
void lightCtrlSetOffDay(int day);
void lightCtrlSetOffTime(const char* offTime);
void lightCtrlPrintSchedule(void);

/**
 * @brief บังคับเปิด/ปิดไฟ
 * @param state true=เปิด, false=ปิด
 */
void lightCtrlSetState(bool state);

/**
 * @brief ดึงสถานะไฟปัจจุบัน
 * @return true ถ้าไฟเปิดอยู่
 */
bool lightCtrlGetState(void);

/**
 * @brief ดึงเวลาปัจจุบัน
 * @param buffer Buffer สำหรับเก็บเวลา (ต้องมีขนาดอย่างน้อย 6 bytes)
 * @param bufferSize ขนาดของ buffer
 * @return true ถ้าอ่านเวลาได้สำเร็จ, false ถ้ายังไม่ได้ sync NTP
 */
bool lightCtrlGetTime(char* buffer, size_t bufferSize);

/**
 * @brief ตรวจสอบว่า schedule mode เปิดอยู่หรือไม่
 * @return true ถ้า lightEnabled = 1
 */
bool lightCtrlIsEnabled(void);
CommandSource lightCtrlGetCommandSource(void);
const char* lightCtrlGetCommandSourceString(CommandSource source);
bool lightCtrlAllowsNetpieControl(void);
bool lightCtrlAllowsLocalControl(void);
void lightCtrlGetConfig(LightControlConfig* config);
void lightCtrlGetStatus(LightControlStatus* status);

/**
 * @brief Getters for schedule parameters
 */
int lightCtrlGetOnDay(void);
const char* lightCtrlGetOnTime(void);
int lightCtrlGetOffDay(void);
const char* lightCtrlGetOffTime(void);

#endif // LIGHT_CONTROLLER_H
