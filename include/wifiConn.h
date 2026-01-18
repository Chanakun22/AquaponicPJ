/**
 * @file wifiConn.h
 * @brief WiFi Connection Manager using WiFiManager library
 * @details จัดการเชื่อมต่อ WiFi แบบ non-blocking
 */

#ifndef WIFI_CONN_H
#define WIFI_CONN_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// PUBLIC FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief เริ่มต้น WiFiManager
 * @note เรียกใช้ใน setup() - non-blocking mode
 */
void wifiSetup(void);

/**
 * @brief ตรวจสอบและจัดการ WiFi connection
 * @note เรียกใช้ใน loop()
 */
void wifiLoop(void);

/**
 * @brief ตรวจสอบสถานะ WiFi
 * @return true ถ้าเชื่อมต่อ WiFi อยู่
 */
bool wifiIsConnected(void);

/**
 * @brief รีเซ็ตการตั้งค่า WiFi (ลบ credentials ที่บันทึกไว้)
 */
void wifiReset(void);

/**
 * @brief ดึง IP Address
 * @param buffer Buffer สำหรับเก็บ IP address (ต้องมีขนาดอย่างน้อย 16 bytes)
 * @param bufferSize ขนาดของ buffer
 * @return true ถ้าเชื่อมต่อและได้ IP address, false ถ้ายังไม่ได้เชื่อมต่อ
 */
bool wifiGetIP(char* buffer, size_t bufferSize);

#endif // WIFI_CONN_H
