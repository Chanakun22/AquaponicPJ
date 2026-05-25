/**
 * @file gpioOut.h
 * @brief Output pin abstraction layer (ESP32 GPIO หรือ MCP23017 I/O expander)
 *
 * รองรับ dual-mode routing per output ผ่าน config flag (OUT_USE_MCP_*)
 * ทำให้ migrate ไป MCP23017 ทีละโมดูลได้โดยไม่ต้องแก้ caller
 *
 * Active-low semantics: gpioOutWrite(out, true) = relay ON (drive LOW)
 *                       gpioOutWrite(out, false) = relay OFF (drive HIGH)
 */

#ifndef GPIO_OUT_H
#define GPIO_OUT_H

#include <Arduino.h>

/**
 * @brief Logical output identifiers (ไม่ผูกกับ pin number ตรงๆ)
 */
typedef enum {
    GPIO_OUT_PUMP_NUTRIENT_A = 0,
    GPIO_OUT_PUMP_NUTRIENT_B,
    GPIO_OUT_LIGHT_RELAY,
    GPIO_OUT_PUMP_CIRCULATION,
    GPIO_OUT_FISH_FEEDER,
    GPIO_OUT_REFILL_ROUTE_VALVE,
    GPIO_OUT_PUMP_REFILL,
    GPIO_OUT_EXHAUST_FAN,
    GPIO_OUT_COUNT
} GpioLogicalOutput;

/**
 * @brief Initialize all logical outputs to OFF state
 *        รวมถึง pinMode() ของ ESP32 GPIO และ init MCP23017 ถ้ามี output ที่ใช้งาน
 * @return true ถ้า init สำเร็จทั้งหมด, false ถ้า MCP23017 ไม่ตอบสนอง
 */
bool gpioOutSetup(void);

/**
 * @brief เขียน state ของ output (active-high logical: true = relay ON, false = OFF)
 *        จะ route ไป ESP32 GPIO หรือ MCP23017 ตาม OUT_USE_MCP_* flag ใน config.h
 */
void gpioOutWrite(GpioLogicalOutput out, bool on);

/**
 * @brief อ่าน last state ที่เขียนไว้ (ไม่ได้อ่านจริงจาก hardware)
 */
bool gpioOutGetLastState(GpioLogicalOutput out);

/**
 * @brief เช็คว่า MCP23017 ตอบสนองอยู่หรือไม่ (i2c ping)
 *        ถ้าไม่มี output ใดใช้ MCP จะ return true เสมอ
 */
bool gpioOutMcpHealthy(void);

/**
 * @brief Re-initialize MCP23017 (ใช้ recovery หลัง i2c bus hang)
 */
bool gpioOutMcpReinit(void);

/**
 * @brief คืน pin number ของ ESP32 GPIO ที่ output นี้ใช้ (ใช้ตอน pinMode/log)
 *        ถ้า output route ไป MCP จะ return -1
 */
int gpioOutGetEsp32Pin(GpioLogicalOutput out);

/**
 * @brief คืน MCP23017 pin number (0-15) ของ output นี้
 *        ถ้า output route ไป ESP32 GPIO จะ return -1
 */
int gpioOutGetMcpPin(GpioLogicalOutput out);

#endif // GPIO_OUT_H
