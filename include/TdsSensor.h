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

/**
 * @brief อ่าน voltage ปัจจุบันของ TDS sensor (สำหรับ calibration)
 * @return voltage ในหน่วย V
 */
float tdsGetVoltage(void);

/**
 * @brief ตั้งค่า 2-Point Calibration
 * @param lowPpm ค่า ppm ของน้ำยามาตรฐานจุดต่ำ
 * @param lowVoltage voltage ที่อ่านได้ที่จุดต่ำ
 * @param highPpm ค่า ppm ของน้ำยามาตรฐานจุดสูง
 * @param highVoltage voltage ที่อ่านได้ที่จุดสูง
 */
void tdsSetCalibration(float lowPpm, float lowVoltage, float highPpm, float highVoltage);

/**
 * @brief ตรวจสอบว่า sensor ได้รับการ calibrate แล้วหรือยัง
 * @return true ถ้า calibrated
 */
bool tdsIsCalibrated(void);

/**
 * @brief ดึงค่า TDS ล่าสุดที่คำนวณแล้วจาก tdsLoop() โดยไม่ทำ analogRead ซ้ำ
 * @return ค่า TDS ในหน่วย ppm หรือ -1 ถ้ายังไม่พร้อม
 */
float tdsGetLastValue(void);

#endif // TDS_SENSOR_H