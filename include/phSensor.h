/**
 * @file phSensor.h
 * @brief อ่านค่า pH จาก Analog pH Sensor (E-201-C)
 * @details รองรับ calibration 3 จุด (pH 4.01, 6.86 และ 9.18)
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
#define PH_OVERSAMPLE_COUNT 8       // จำนวน analogRead ต่อ 1 รอบ sampling
#define PH_INVALID_STREAK_LIMIT 3   // ต้อง invalid ติดต่อกันกี่รอบจึง mark ว่าหลุดช่วง
#define PH_VOLTAGE_FILTER_ALPHA 0.18f // Processing EMA for pH calculation: faster but still damped
#define PH_VOLTAGE_DISPLAY_FILTER_ALPHA 0.06f // Extra smoothing just for displayed voltage (mV)
#define PH_PH_FILTER_ALPHA    0.18f // Balanced pH EMA: quicker response without going fully raw
#define PH_VOLTAGE_DEADBAND_MV 3.0f // Smaller process deadband so pH value moves sooner
#define PH_VOLTAGE_DISPLAY_DEADBAND_MV 8.0f // Larger display deadband so calibration voltage looks steadier
#define PH_PH_DEADBAND        0.01f // Allow smaller visible pH movement before holding value
#define PH_PH_MAX_STEP        0.12f // Let pH catch up faster per cycle while still limiting jumps
#define PH_ADC_SETTLE_US      250   // Quiet time before each pH ADC sample after analog channel activity
#define PH_ADC_DUMMY_READS    2     // Discard initial ADC reads so pH sampling starts after channel settles

// Calibration values (ต้อง calibrate ใหม่ตาม sensor จริง)
#define PH_VOLTAGE_AT_686   2058    // ADC value ที่ pH 6.86 (neutral-ish)
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
 * @brief Calibrate sensor ที่ pH 6.86
 * @note จุ่ม probe ใน buffer pH 6.86 แล้วเรียกฟังก์ชันนี้
 */
void phCalibratePh686(void);

/**
 * @brief Calibrate sensor ที่ pH 4.01
 * @note จุ่ม probe ใน buffer pH 4.01 แล้วเรียกฟังก์ชันนี้
 */
void phCalibratePh401(void);

/**
 * @brief Calibrate sensor ที่ pH 9.18
 * @note จุ่ม probe ใน buffer pH 9.18 แล้วเรียกฟังก์ชันนี้
 */
void phCalibratePh918(void);

/**
 * @brief ตรวจสอบว่าจุด pH 4.01 ถูก calibrate แล้วหรือยัง
 */
bool phHasCalibration401(void);

/**
 * @brief ตรวจสอบว่าจุด pH 6.86 ถูก calibrate แล้วหรือยัง
 */
bool phHasCalibration686(void);

/**
 * @brief ตรวจสอบว่าจุด pH 9.18 ถูก calibrate แล้วหรือยัง
 */
bool phHasCalibration918(void);

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
