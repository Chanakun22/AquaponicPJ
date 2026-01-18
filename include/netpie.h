/**
 * @file netpie.h
 * @brief NETPIE IoT Platform Connection via MQTT
 * @details ส่งและรับข้อมูลจาก NETPIE
 */

#ifndef NETPIE_H
#define NETPIE_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้น NETPIE MQTT Client
 * @note เรียกใช้ใน setup() หลังจาก WiFi connected
 */
void netpieSetup(void);

/**
 * @brief จัดการ MQTT connection และ messages
 * @note เรียกใช้ใน loop()
 */
void netpieLoop(void);

/**
 * @brief ตรวจสอบสถานะ MQTT connection
 * @return true ถ้าเชื่อมต่อ NETPIE อยู่
 */
bool netpieIsConnected(void);

/**
 * @brief ส่งข้อมูลเซ็นเซอร์ไป NETPIE Shadow
 * @param waterTemp อุณหภูมิน้ำ (°C)
 * @param airTemp อุณหภูมิอากาศ (°C)
 * @param humidity ความชื้น (%)
 * @param tds ค่า TDS (ppm)
 * @param light ความเข้มแสง (lux)
 * @param ph ค่า pH (0-14)
 */
void netpiePublishData(float waterTemp, float airTemp, float humidity, float tds, float light, float ph);

/**
 * @brief ส่งข้อความไปยัง topic ที่กำหนด
 * @param topic MQTT topic
 * @param payload ข้อความที่ต้องการส่ง
 */
void netpiePublish(const char* topic, const char* payload);

#endif // NETPIE_H
