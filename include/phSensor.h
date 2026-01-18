/**
 * @file phSensor.h
 * @brief อ่านค่า pH จาก Analog pH Sensor (E-201-C)
 * @details รองรับ calibration 2 จุด (pH 4.0 และ pH 7.0)
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

// Calibration values (ต้อง calibrate ใหม่ตาม sensor จริง)
#define PH_VOLTAGE_AT_7     2048    // ADC value ที่ pH 7.0 (neutral)
#define PH_VOLTAGE_SLOPE    -59.16  // mV per pH unit at 25°C

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้น pH Sensor
 */
void phSetup(void);

/**
 * @brief วนลูปอ่านค่า pH
 * @note เรียกใช้ใน loop()
 */
void phLoop(void);

/**
 * @brief อ่านค่า pH ปัจจุบัน
 * @return ค่า pH (0-14) หรือ -1 ถ้ายังไม่พร้อม
 */
float phRead(void);

/**
 * @brief อ่านค่า voltage จาก sensor
 * @return voltage (mV)
 */
float phReadVoltage(void);

/**
 * @brief ตรวจสอบว่า sensor พร้อมหรือยัง
 * @return true ถ้าพร้อมใช้งาน
 */
bool phIsReady(void);

/**
 * @brief Calibrate sensor ที่ pH 7.0
 * @note จุ่ม probe ใน buffer pH 7.0 แล้วเรียกฟังก์ชันนี้
 */
void phCalibratePh7(void);

/**
 * @brief Calibrate sensor ที่ pH 4.0
 * @note จุ่ม probe ใน buffer pH 4.0 แล้วเรียกฟังก์ชันนี้
 */
void phCalibratePh4(void);

/**
 * @brief ตั้งค่าอุณหภูมิน้ำสำหรับ Temperature Compensation
 * @param temperature อุณหภูมิน้ำ (°C)
 */
void phSetTemperature(float temperature);

/**
 * @brief ลบค่า Calibration ที่บันทึกไว้ใน NVS
 */
void phClearCalibration(void);

#endif // PH_SENSOR_H
