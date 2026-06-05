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

typedef enum {
    TEMP_CHANNEL_MIX = 0,
    TEMP_CHANNEL_FISH,
    TEMP_CHANNEL_COUNT
} TempChannel;

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
 * @brief อ่านค่าอุณหภูมิน้ำตาม channel
 */
float tempGetTemperature(TempChannel channel);

/**
 * @brief จำนวน DS18B20 ที่ scan เจอบน OneWire bus
 */
int tempGetDeviceCount(void);

/**
 * @brief รีเฟรชผลการ scan DS18B20 บน OneWire bus แบบ realtime
 * @return จำนวน device ที่พบล่าสุด
 */
int tempRefreshScan(void);

/**
 * @brief อ่าน address ของ DS18B20 ที่ scan เจอเป็น hex string
 */
bool tempGetScannedAddressHex(uint8_t index, char* out, size_t outSize);

/**
 * @brief อ่าน address ที่ bind กับ channel เป็น hex string
 */
bool tempGetBoundAddressHex(TempChannel channel, char* out, size_t outSize);

/**
 * @brief bind channel เข้ากับ DS18B20 จาก scan index แล้ว save ลง NVS
 */
bool tempBindChannelToIndex(TempChannel channel, uint8_t index);

/**
 * @brief สลับ binding ระหว่าง mix/fish แล้ว save ลง NVS
 */
bool tempSwapChannels(void);

/**
 * @brief ฟังก์ชันหลักสำหรับเรียกใน loop()
 * @note ใช้ non-blocking delay อ่านค่าทุก TEMP_READ_INTERVAL ms
 */
void tempLoop(void);

#endif // TEMP_SENSOR_H