/**
 * @file wifiConn.h
 * @brief WiFi Connection Manager using WiFiManager library
 * @details จัดการเชื่อมต่อ WiFi ด้วย WiFiManager
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
 * @note เรียกใช้ใน setup() - blocking จนกว่าจะเชื่อมต่อหรือ timeout
 */
void wifiSetup(void);

/**
 * @brief ตรวจสอบและจัดการ WiFi connection
 * @note เรียกใช้ใน loop() - non-blocking
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

/**
 * @brief แจ้งว่า Web Server พร้อมแล้ว (unused in blocking mode)
 */
void wifiMarkServerStarted(void);

#endif // WIFI_CONN_H
