/**
 * @file dataApi.h
 * @brief Simple HTTP API for sensor data (Test/Monitor)
 * @note ไฟล์นี้สำหรับ Test เท่านั้น - ลบได้ถ้าไม่ใช้
 */

#ifndef DATA_API_H
#define DATA_API_H

#include <Arduino.h>

/**
 * @brief Initialize HTTP server for data API
 */
void dataApiSetup(void);

/**
 * @brief Handle HTTP requests (call in loop)
 */
void dataApiLoop(void);

#endif // DATA_API_H
