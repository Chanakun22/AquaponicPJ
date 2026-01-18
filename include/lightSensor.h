/**
 * @file lightSensor.h
 * @brief ไลบรารีสำหรับเซ็นเซอร์ BH1750 (Light Sensor)
 * @details วัดความเข้มแสง (Lux)
 */

#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้นการทำงานของ BH1750 Light Sensor
 * @note เรียกใช้ใน setup()
 */
void lightSetup(void);

/**
 * @brief อ่านค่าความเข้มแสง
 * @return ความเข้มแสงในหน่วย Lux (คืนค่า -1 ถ้าอ่านไม่ได้)
 */
float lightRead(void);

/**
 * @brief ฟังก์ชันหลักสำหรับเรียกใน loop()
 * @note ใช้ non-blocking delay อ่านค่าทุก LIGHT_READ_INTERVAL ms
 */
void lightLoop(void);

/**
 * @brief ตรวจสอบว่าเซ็นเซอร์พร้อมใช้งานหรือไม่
 * @return true ถ้าเซ็นเซอร์ทำงานปกติ
 */
bool lightIsReady(void);

#endif // LIGHT_SENSOR_H
