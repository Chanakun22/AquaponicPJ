/**
 * @file tempSensor.h
 * @brief ไลบรารีสำหรับเซ็นเซอร์ DS18B20 (อุณหภูมิน้ำ)
 * @details วัดอุณหภูมิน้ำด้วย Dallas DS18B20 ผ่าน OneWire
 */

#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้นการทำงานของ Temperature Sensor
 * @note เรียกใช้ใน setup()
 */
void tempSetup(void);

/**
 * @brief อ่านค่าอุณหภูมิน้ำ
 * @return อุณหภูมิในหน่วย °C
 */
float tempRead(void);

/**
 * @brief ฟังก์ชันหลักสำหรับเรียกใน loop()
 * @note ใช้ non-blocking delay อ่านค่าทุก TEMP_READ_INTERVAL ms
 */
void tempLoop(void);

#endif // TEMP_SENSOR_H