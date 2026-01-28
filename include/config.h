/**
 * @file config.h
 * @brief ไฟล์ตั้งค่ารวม (Central Configuration)
 * @details รวม Pin Configuration และค่าคงที่ทั้งหมดไว้ที่เดียว
 *          เพื่อให้ง่ายต่อการแก้ไขและอ่านโค้ด
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "secrets.h"

// ============================================================================
// HARDWARE PIN CONFIGURATION
// ============================================================================

/**
 * @section Sensor Pins
 * @brief กำหนดขา GPIO สำหรับเซ็นเซอร์ต่างๆ
 */

// TDS Sensor
#define TDS_PIN             5       // ขา Analog สำหรับ TDS Sensor

// DHT22 Sensor (Temperature & Humidity)
#define DHT_PIN             15      // ขา Digital สำหรับ DHT22

// DS18B20 Temperature Sensor (OneWire)
#define ONE_WIRE_PIN        4       // ขา OneWire สำหรับ DS18B20

// pH Sensor (Analog)
#define PH_SENSOR_PIN       6       // ขา Analog สำหรับ pH Sensor

// LED
#define STATUS_LED_PIN      LED_BUILTIN  // ขา LED แสดงสถานะ

// I2C Bus (สำหรับ BH1750)
#define I2C_SDA_PIN         8       // ขา SDA สำหรับ I2C
#define I2C_SCL_PIN         9       // ขา SCL สำหรับ I2C

// Relay/Light Control
#define LIGHT_RELAY_PIN     LED_BUILTIN  // ใช้ LED บนบอร์ดทดสอบ

// ============================================================================
// SENSOR CONFIGURATION
// ============================================================================

/**
 * @section TDS Sensor Settings
 */
#define TDS_VREF            3.3     // แรงดันอ้างอิง (V) - ESP32 = 3.3V
#define TDS_ADC_RESOLUTION  4096.0  // ความละเอียด ADC (12-bit = 4096)
#define TDS_SAMPLE_COUNT    30      // จำนวนจุด sampling

/**
 * @section Sensor Value Validation Ranges
 * @brief ช่วงค่าที่ยอมรับได้สำหรับแต่ละ sensor
 */
#define TDS_MIN             0.0f    // TDS ต่ำสุด (ppm)
#define TDS_MAX             2000.0f // TDS สูงสุด (ppm)

#define PH_MIN              0.0f    // pH ต่ำสุด
#define PH_MAX              14.0f   // pH สูงสุด

#define TEMP_MIN            0.0f    // อุณหภูมิต่ำสุด (°C)
#define TEMP_MAX            50.0f   // อุณหภูมิสูงสุด (°C)

#define HUMIDITY_MIN        0.0f    // ความชื้นต่ำสุด (%)
#define HUMIDITY_MAX        100.0f  // ความชื้นสูงสุด (%)

#define LIGHT_MIN           0.0f    // ความเข้มแสงต่ำสุด (lux)
#define LIGHT_MAX           65535.0f // ความเข้มแสงสูงสุด (lux)

/**
 * @section DHT Sensor Settings
 */
#define DHT_TYPE            DHT22   // ชนิดเซ็นเซอร์ (DHT11, DHT22, DHT21)
#define DHT_READ_INTERVAL   2000    // ช่วงเวลาอ่านค่า (ms)

/**
 * @section DS18B20 Settings
 */
#define TEMP_READ_INTERVAL  1000    // ช่วงเวลาอ่านค่า (ms)

/**
 * @section BH1750 Light Sensor Settings
 */
#define BH1750_ADDRESS      0x23    // I2C address (ADDR=LOW: 0x23, ADDR=HIGH: 0x5C)
#define LIGHT_READ_INTERVAL 2000    // ช่วงเวลาอ่านค่า (ms)

// ============================================================================
// SYSTEM CONFIGURATION
// ============================================================================

/**
 * @section Serial Settings
 */
#define SERIAL_BAUD_RATE    115200  // Baud rate สำหรับ Serial Monitor

/**
 * @section Logging Configuration
 * LOG_LEVEL_INFO = Production (no debug)
 * LOG_LEVEL_DEBUG = Development (all logs)
 */
#define LOG_LEVEL           LOG_LEVEL_INFO

/**
 * @section OTA Update Settings
 */
#define OTA_ENABLED         1       // 1=Enable OTA, 0=Disable
#define OTA_PORT            3232     // OTA update port
#define OTA_HOSTNAME        "aquaponics-sensor"  // OTA hostname
#define OTA_PASSWORD        SECRET_OTA_PASSWORD

/**
 * @section Factory Reset Settings
 */
#define FACTORY_RESET_PIN   0       // Pin for factory reset (BOOT button)
#define FACTORY_RESET_TIME  5000    // Hold button for 5 seconds to reset

/**
 * @section Timing Settings
 */
#define TDS_READ_INTERVAL   1000    // ช่วงเวลาอ่านค่า TDS (ms)

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================

/**
 * @section WiFiManager Settings
 * @brief ตั้งค่า Access Point สำหรับ WiFiManager
 */
#define WIFI_AP_NAME        SECRET_WIFI_AP_NAME
#define WIFI_AP_PASSWORD    SECRET_WIFI_AP_PASS
#define WIFI_CONNECT_TIMEOUT 180              // Timeout เชื่อมต่อ (วินาที)
#define WIFI_CHECK_INTERVAL  30000            // ตรวจสอบสถานะทุก (ms)

// ============================================================================
// NETPIE CONFIGURATION
// ============================================================================

/**
 * @section NETPIE MQTT Settings
 * @brief ข้อมูลสำหรับเชื่อมต่อ NETPIE IoT Platform
 */
#define NETPIE_CLIENT_ID    SECRET_NETPIE_CLIENT_ID
#define NETPIE_TOKEN        SECRET_NETPIE_TOKEN
#define NETPIE_SECRET       SECRET_NETPIE_SECRET

/**
 * @section MQTT Broker Settings
 */
#define MQTT_BROKER         "mqtt.netpie.io"
#define MQTT_PORT           1883
#define MQTT_RECONNECT_INTERVAL 5000          // รอ reconnect ทุก (ms)

/**
 * @section Data Publishing Settings
 */
#define NETPIE_PUBLISH_INTERVAL 60000         // ส่งข้อมูลทุก (ms) = 60 วินาที

// ============================================================================
// NTP TIME CONFIGURATION
// ============================================================================

/**
 * @section NTP Settings
 * @brief ตั้งค่าเวลาสำหรับ Light Schedule
 */
#define NTP_SERVER          "pool.ntp.org"
#define GMT_OFFSET_SEC      25200             // GMT+7 (Thailand) = 7*3600 = 25200
#define DAYLIGHT_OFFSET_SEC 0                 // ไม่มี Daylight Saving

// ============================================================================
// LIGHT CONTROLLER CONFIGURATION
// ============================================================================

/**
 * @section Light Schedule Settings
 */
#define LIGHT_CHECK_INTERVAL 1000             // ตรวจสอบตารางเวลาทุก (ms)

// ============================================================================
// WATCHDOG TIMER CONFIGURATION
// ============================================================================

/**
 * @section Watchdog Timer Settings
 * @brief ตั้งค่า Hardware Watchdog Timer เพื่อป้องกันระบบ hang
 */
#define WATCHDOG_TIMEOUT_SEC  60              // Watchdog timeout (วินาที)
#define WATCHDOG_ENABLED      1               // 1=เปิด Watchdog, 0=ปิด

#endif // CONFIG_H
