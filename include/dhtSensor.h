/**
 * @file dhtSensor.h
 * @brief ไลบรารีสำหรับเซ็นเซอร์ DHT22 (อุณหภูมิและความชื้น)
 * @details วัดอุณหภูมิ (°C) และความชื้นสัมพัทธ์ (%)
 */

#ifndef DHT_SENSOR_H
#define DHT_SENSOR_H

#include <Arduino.h>
#include <DHT.h>
#include "config.h"

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้นการทำงานของ DHT Sensor
 * @note เรียกใช้ใน setup()
 */
void dhtSetup(void);

/**
 * @brief อ่านค่าอุณหภูมิ
 * @return อุณหภูมิในหน่วย °C (คืนค่า NAN ถ้าอ่านไม่ได้)
 */
float dhtReadTemperature(void);

/**
 * @brief อ่านค่าความชื้น
 * @return ความชื้นในหน่วย % (คืนค่า NAN ถ้าอ่านไม่ได้)
 */
float dhtReadHumidity(void);

/**
 * @brief ฟังก์ชันหลักสำหรับเรียกใน loop()
 * @note ใช้ non-blocking delay อ่านค่าทุก DHT_READ_INTERVAL ms
 */
void dhtLoop(void);

#endif // DHT_SENSOR_H