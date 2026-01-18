/**
 * @file TdsSensor.h
 * @brief ไลบรารีสำหรับเซ็นเซอร์วัดค่า TDS (Total Dissolved Solids)
 * @details ใช้วัดปริมาณของแข็งละลายน้ำ หน่วย ppm
 */

#ifndef TDS_SENSOR_H
#define TDS_SENSOR_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้นการทำงานของ TDS Sensor
 * @note เรียกใช้ใน setup()
 */
void tdsSetup(void);

/**
 * @brief อ่านค่า TDS พร้อมชดเชยอุณหภูมิ
 * @param temperature อุณหภูมิปัจจุบัน (°C) สำหรับชดเชยค่า
 * @return ค่า TDS ในหน่วย ppm
 */
float tdsRead(float temperature);

/**
 * @brief ฟังก์ชันหลักสำหรับเรียกใน loop()
 * @param temperature อุณหภูมิปัจจุบัน (°C)
 * @note ใช้ non-blocking delay อ่านค่าทุก TDS_READ_INTERVAL ms
 */
void tdsLoop(float temperature);

/**
 * @brief ตรวจสอบว่าเก็บ sample ครบแล้วหรือยัง
 * @return true ถ้าพร้อมแสดงผล, false ถ้ายังเก็บไม่ครบ
 */
bool tdsIsReady(void);

#endif // TDS_SENSOR_H