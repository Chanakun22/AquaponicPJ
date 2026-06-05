/**
 * @file config.h
 * @brief ไฟล์ตั้งค่ารวม (Central Configuration)
 * @details รวม Pin Configuration และค่าคงที่ทั้งหมดไว้ที่เดียว
 *          เพื่อให้ง่ายต่อการแก้ไขและอ่านโค้ด
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "controlSource.h"
#include "secrets.h"

// ============================================================================
// HARDWARE PIN CONFIGURATION
// ============================================================================

/**
 * @section Sensor Pins
 * @brief กำหนดขา GPIO สำหรับเซ็นเซอร์ต่างๆ
 */

// TDS Sensor
#define TDS_PIN             5       // ขา Analog สำหรับ TDS Sensor เดิม (mix tank)
#define TDS_MIX_PIN         TDS_PIN // alias สำหรับ multi-channel refactor
#define TDS_FISH_PIN        7       // ขา Analog สำหรับ TDS Sensor ตู้ปลา

// DHT22 Sensor (Temperature & Humidity)
#define DHT_PIN             15      // ขา Digital สำหรับ DHT22

// DS18B20 Temperature Sensor (OneWire)
#define ONE_WIRE_PIN        13      // ขา OneWire สำหรับ DS18B20

// pH Sensor (Analog)
#define PH_SENSOR_PIN       6       // legacy alias = mix tank probe (kept for backward compat)
#define PH_SENSOR_MIX_PIN   6       // ขา Analog สำหรับ pH Sensor ถังผสม
#define PH_SENSOR_FISH_PIN  1       // ขา Analog สำหรับ pH Sensor ตู้ปลา (ADC1_CH0)

// LED
#define STATUS_LED_PIN      LED_BUILTIN  // ขา LED แสดงสถานะ

// I2C Bus (สำหรับ BH1750)
#define I2C_SDA_PIN         8       // ขา SDA สำหรับ I2C
#define I2C_SCL_PIN         9       // ขา SCL สำหรับ I2C
#define I2C_CLOCK_HZ        100000  // 100kHz เพื่อความเสถียรเมื่อแชร์ BH1750 + MCP23017

// MCP23017 I/O Expander (output relay control)
// ใช้ I2C bus เดียวกับ BH1750 (default address 0x20)
#define MCP23017_I2C_ADDR   0x20    // A0=A1=A2=GND
#define MCP23017_RESET_PIN  4       // ESP32 GPIO ที่ control RESET pin ของ MCP

// MCP23017 logical pin assignments (GPA0-7 = pins 0-7, GPB0-7 = pins 8-15)
#define MCP_PIN_PUMP_NUTRIENT_A   0   // GPA0
#define MCP_PIN_PUMP_NUTRIENT_B   1   // GPA1
#define MCP_PIN_LIGHT_RELAY       2   // GPA2
#define MCP_PIN_PUMP_CIRCULATION  3   // GPA3
#define MCP_PIN_FISH_FEEDER       4   // GPA4
#define MCP_PIN_REFILL_ROUTE_VALVE 5  // GPA5
#define MCP_PIN_PUMP_REFILL       6   // GPA6
#define MCP_PIN_EXHAUST_FAN       7   // GPA7

// Per-output routing flag: ถ้า 1 = ใช้ MCP23017, ถ้า 0 = ใช้ ESP32 GPIO เดิม
// เริ่มต้น default ทุกตัว = 0 (ESP32 GPIO) เพื่อให้ behavior เหมือนเดิม
// flip ทีละตัวเป็น 1 หลัง wire MCP23017 และทดสอบทีละโมดูล
#ifndef OUT_USE_MCP_PUMP_NUTRIENT_A
#define OUT_USE_MCP_PUMP_NUTRIENT_A   1
#endif
#ifndef OUT_USE_MCP_PUMP_NUTRIENT_B
#define OUT_USE_MCP_PUMP_NUTRIENT_B   1
#endif
#ifndef OUT_USE_MCP_LIGHT_RELAY
#define OUT_USE_MCP_LIGHT_RELAY       1
#endif
#ifndef OUT_USE_MCP_PUMP_CIRCULATION
#define OUT_USE_MCP_PUMP_CIRCULATION  1
#endif
#ifndef OUT_USE_MCP_FISH_FEEDER
#define OUT_USE_MCP_FISH_FEEDER       0
#endif
#ifndef OUT_USE_MCP_REFILL_ROUTE_VALVE
#define OUT_USE_MCP_REFILL_ROUTE_VALVE 1
#endif
#ifndef OUT_USE_MCP_PUMP_REFILL
#define OUT_USE_MCP_PUMP_REFILL       1
#endif
#ifndef OUT_USE_MCP_EXHAUST_FAN
#define OUT_USE_MCP_EXHAUST_FAN       1
#endif

// Relay/Light Control
#define LIGHT_RELAY_PIN     12       // ขา Relay ควบคุมไฟ NeoPixel

#ifndef FISH_FEEDER_PIN
#define FISH_FEEDER_PIN     21       // รีเลย์/มอเตอร์ให้อาหารปลา
#endif

#ifndef EXHAUST_FAN_PIN
#define EXHAUST_FAN_PIN     2     // พัดลมระบายอากาศ/ดูดอากาศ
#endif

// Automation Pumps (Dosing & Water)
#define PUMP_NUTRIENT_A_PIN 10      // ปั๊มปุ๋ย A
#define PUMP_NUTRIENT_B_PIN 11      // ปั๊มปุ๋ย B

// Water System (set GPIO when hardware wiring is finalized)
#ifndef PUMP_CIRCULATION_PIN
#define PUMP_CIRCULATION_PIN 16     // ปั๊มน้ำวนหลัก
#endif

#ifndef PUMP_REFILL_PIN
#define PUMP_REFILL_PIN     42     // ปั๊มเติมน้ำจากถังน้ำสะอาดเข้าตู้ปลา (หลบขา 33-37 ที่อาจชน flash/PSRAM)
#endif

#ifndef REFILL_ROUTE_VALVE_PIN
#define REFILL_ROUTE_VALVE_PIN 39   // โซลินอยด์น้ำเข้าถังผสม (ใช้ชื่อเดิมเพื่อคง compatibility)
#endif

#ifndef SUMP_LEVEL_LOW_PIN
#define SUMP_LEVEL_LOW_PIN   40     // เซ็นเซอร์ระดับน้ำต่ำถังรวม
#endif

#ifndef SUMP_LEVEL_HIGH_PIN
#define SUMP_LEVEL_HIGH_PIN  41     // เซ็นเซอร์ระดับน้ำสูงถังรวม
#endif

#ifndef FISH_TANK_OVERFLOW_PIN
#define FISH_TANK_OVERFLOW_PIN 47  // เซ็นเซอร์กันล้นตู้ปลา
#endif


// Relay Logic (Active Low: LOW = ON, HIGH = OFF)
#define PUMP_ON   LOW
#define PUMP_OFF  HIGH
#define REFILL_ROUTE_TO_FISH_STATE PUMP_OFF
#define REFILL_ROUTE_TO_SUMP_STATE PUMP_ON

// ============================================================================
// SENSOR CONFIGURATION
// ============================================================================

/**
 * @section TDS Sensor Settings
 */
#define TDS_VREF            3.3     // แรงดันอ้างอิง ADC ของ ESP32 (ไม่ใช่ไฟเลี้ยง sensor)
#define TDS_ADC_RESOLUTION  4096.0  // ความละเอียด ADC (12-bit = 4096)
#define TDS_SAMPLE_COUNT    30      // จำนวนจุด sampling
#define TDS_OVERSAMPLE_COUNT 8      // จำนวน analogRead ต่อ 1 รอบ sampling เพื่อลด noise
#define TDS_ADC_SETTLE_US   250     // Quiet time หลัง analog channel activity ก่อนเก็บ sample
#define TDS_ADC_DUMMY_READS 2       // ทิ้ง sample แรก ๆ เพื่อให้ ADC settle ก่อนอ่านจริง
#define TDS_CONVERSION_FACTOR 0.695f // แปลง EC25 -> TDS ให้ใกล้ handheld meter ที่อ่าน 1413 uS/cm ≈ 982 ppm
#define TDS_MIN_CALIBRATION_SPAN_V 0.050f // calibration 2 จุดต้องต่างกันพอ ไม่งั้นจะขยาย noise จาก ADC มากเกินไป
#define TDS_VOLTAGE_FILTER_ALPHA 0.15f // EMA สำหรับแรงดันดิบหลัง median
#define TDS_VOLTAGE_DEADBAND_V 0.003f  // กันแรงดันแกว่งเล็กน้อยไม่ให้โชว์สั่น
#define TDS_VALUE_FILTER_ALPHA 0.10f   // EMA สำหรับค่า TDS หลังชดเชยอุณหภูมิ
#define TDS_VALUE_DEADBAND_PPM 5.0f    // กันค่า TDS แกว่งเล็กน้อยไม่ให้เปลี่ยนผลลัพธ์ทันที
#define TDS_VALUE_MAX_STEP_PPM 20.0f   // จำกัดการกระโดดต่อรอบเพื่อให้ค่าดูนิ่งขึ้น

/**
 * @section Sensor Value Validation Ranges
 * @brief ช่วงค่าที่ยอมรับได้สำหรับแต่ละ sensor
 */
#define TDS_MIN             0.0f    // TDS ต่ำสุด (ppm)
#define TDS_MAX             2000.0f // TDS สูงสุด (ppm)

#define PH_MIN              0.0f    // pH ต่ำสุด
#define PH_MAX              14.0f   // pH สูงสุด
#define PH_CAL_POINT_401    4.01f   // จุด buffer กรด
#define PH_CAL_POINT_686    6.86f   // จุด buffer กลาง
#define PH_CAL_POINT_918    9.18f   // จุด buffer ด่าง

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
#define DHT_SLOW_READ_WARN_MS 250   // เตือนถ้า DHT read ใช้เวลานานผิดปกติ
#define DHT_MAX_CONSECUTIVE_FAILURES 3 // จำนวน fail ติดกันก่อนพัก sensor ชั่วคราว
#define DHT_FAIL_BACKOFF_MS 10000    // พักการอ่าน DHT หลัง fail ต่อเนื่อง (ms)

/**
 * @section DS18B20 Settings
 */
#define TEMP_READ_INTERVAL  2000    // ช่วงเวลาอ่านค่า (ms)

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
#ifndef LOG_LEVEL
#define LOG_LEVEL           LOG_LEVEL_INFO
#endif



/**
 * @section Factory Reset Settings
 */
#define FACTORY_RESET_PIN   0       // Pin for factory reset (BOOT button)
#define FACTORY_RESET_TIME  5000    // Hold button for 5 seconds to reset

/**
 * @section Timing Settings
 */
#define TDS_READ_INTERVAL   1000    // ช่วงเวลาอ่านค่า TDS (ms)

// Helper Macros
#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(s) #s

// ==========================================
// WIFI Configuration
// ==========================================
#ifndef SECRET_WIFI_AP_NAME
#define SECRET_WIFI_AP_NAME Aquaponics-LAN // Default if not defined
#endif
#ifndef SECRET_WIFI_AP_PASS
#define SECRET_WIFI_AP_PASS __CONFIGURE_ME__
#endif

#define WIFI_AP_NAME        XSTRINGIFY(SECRET_WIFI_AP_NAME)
#define WIFI_AP_PASS        XSTRINGIFY(SECRET_WIFI_AP_PASS)
#define WIFI_CONNECT_TIMEOUT 180              // Timeout เชื่อมต่อ (วินาที)
#define WIFI_CHECK_INTERVAL  30000            // ตรวจสอบสถานะทุก (ms)

// ==========================================
// OTA Configuration
// ==========================================
#define OTA_ENABLED         1       // 1=Enable OTA, 0=Disable
#define OTA_PORT            3232     // OTA update port
#define OTA_HOSTNAME        "aquaponics-sensor"  // OTA hostname
#define UNCONFIGURED_SECRET_SENTINEL "__CONFIGURE_ME__"

#ifndef SECRET_OTA_PASSWORD
#define SECRET_OTA_PASSWORD __CONFIGURE_ME__
#endif

#define OTA_PASSWORD        XSTRINGIFY(SECRET_OTA_PASSWORD)

// ==========================================
// NETPIE MQTT Configuration
// ==========================================
#define MQTT_BROKER         "mqtt.netpie.io"
#define MQTT_PORT           1883
#define MQTT_RECONNECT_INTERVAL 5000          // รอ reconnect ทุก (ms)
#define NETPIE_PUBLISH_INTERVAL 10000         // ส่งข้อมูลทุก (ms) = 10 วินาที

#ifndef SECRET_NETPIE_CLIENT_ID
#define SECRET_NETPIE_CLIENT_ID unknown_client
#endif
#ifndef SECRET_NETPIE_TOKEN
#define SECRET_NETPIE_TOKEN     unknown_token
#endif
#ifndef SECRET_NETPIE_SECRET
#define SECRET_NETPIE_SECRET    unknown_secret
#endif

#define NETPIE_CLIENT_ID    XSTRINGIFY(SECRET_NETPIE_CLIENT_ID)
#define NETPIE_TOKEN        XSTRINGIFY(SECRET_NETPIE_TOKEN)
#define NETPIE_SECRET       XSTRINGIFY(SECRET_NETPIE_SECRET)

// ==========================================
// Telnet Configuration
// ==========================================
#define TELNET_PORT         23

#ifndef SECRET_TELNET_PASSWORD
#define SECRET_TELNET_PASSWORD __CONFIGURE_ME__
#endif

#define TELNET_PASSWORD     XSTRINGIFY(SECRET_TELNET_PASSWORD)

// ============================================================================
// LOCAL MQTT CONFIGURATION (RASPBERRY PI)
// ============================================================================

/**
 * @section Local MQTT Settings
 * @brief เชื่อมต่อกับ Raspberry Pi ภายในวง LAN
 */
#define LOCAL_MQTT_HOSTNAME "Chanakun"          // ชื่อ Hostname ของ Pi (ตามที่เห็นใน Router/Terminal)
#define LOCAL_MQTT_STATIC_IP "192.168.10.1"     // Pi IP บน AP network (Fallback เมื่อ mDNS หาไม่เจอ)
#define LOCAL_MQTT_PORT     1883
#define LOCAL_PUBLISH_INTERVAL 2000          // ส่งข้อมูลทุก 2 วินาที (เร็วกว่า NETPIE)
#define LOCAL_MQTT_TOPIC_SENSORS "aquaponics/sensors"  // MQTT Topic สำหรับข้อมูล Sensor
#define LOCAL_MQTT_TOPIC_LOGS    "aquaponics/logs"     // MQTT Topic สำหรับ System Logs
#define LOCAL_MQTT_TOPIC_CONFIG_SENSORS "aquaponics/config/sensors" // MQTT Topic สำหรับ Config Sensors (Pi -> ESP)
#define LOCAL_MQTT_TOPIC_STATUS_SENSORS "aquaponics/status/sensors" // MQTT Topic สำหรับ Status Sensors (ESP -> Pi)

#define LOCAL_MQTT_TOPIC_CONFIG_AUTOMATION "aquaponics/config/automation" // MQTT Topic สำหรับกำหนดเป้าหมาย TDS/pH
#define LOCAL_MQTT_TOPIC_STATUS_AUTOMATION "aquaponics/status/automation" // MQTT Topic แจ้งสถานะเป้าหมายปัจจุบัน

#define LOCAL_MQTT_TOPIC_CONFIG_FAN_CONTROL "aquaponics/config/fan_control" // MQTT Topic สำหรับพัดลมระบายอากาศ (Pi -> ESP)
#define LOCAL_MQTT_TOPIC_STATUS_FAN_CONTROL "aquaponics/status/fan_control" // MQTT Topic สถานะพัดลมระบายอากาศ (ESP -> Pi)

#define LOCAL_MQTT_TOPIC_CONFIG_LIGHT_CONTROL "aquaponics/config/light_control" // MQTT Topic สำหรับ light controller
#define LOCAL_MQTT_TOPIC_STATUS_LIGHT_CONTROL "aquaponics/status/light_control" // MQTT Topic สถานะ light controller

#define LOCAL_MQTT_TOPIC_CONFIG_FISH_FEEDER "aquaponics/config/fish_feeder" // MQTT Topic สำหรับ fish feeder
#define LOCAL_MQTT_TOPIC_STATUS_FISH_FEEDER "aquaponics/status/fish_feeder" // MQTT Topic สถานะ fish feeder

#define LOCAL_MQTT_TOPIC_CONFIG_WATER_SYSTEM "aquaponics/config/water_system" // MQTT Topic สำหรับระบบน้ำ (Pi -> ESP)
#define LOCAL_MQTT_TOPIC_STATUS_WATER_SYSTEM "aquaponics/status/water_system" // MQTT Topic สถานะระบบน้ำ (ESP -> Pi)

#define LOCAL_MQTT_TOPIC_HW_TEST_CMD    "aquaponics/test/command"  // Pi → ESP: สั่งทดสอบ Hardware
#define LOCAL_MQTT_TOPIC_HW_TEST_RESULT "aquaponics/test/result"   // ESP → Pi: ผลลัพธ์การทดสอบ

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
#define LIGHT_DEFAULT_COMMAND_SOURCE COMMAND_SOURCE_NETPIE

// ============================================================================
// FISH FEEDER CONFIGURATION
// ============================================================================

#define FEEDER_CHECK_INTERVAL_MS          1000UL
#define FEEDER_DEFAULT_COMMAND_SOURCE     COMMAND_SOURCE_LOCAL_WEB
#define FEEDER_DEFAULT_ENABLED            0
#define FEEDER_DEFAULT_FEED_DAY           7
#define FEEDER_DEFAULT_FEED_HOUR          8
#define FEEDER_DEFAULT_FEED_MINUTE        0
#define FEEDER_DEFAULT_DURATION_MS        2000UL
#define FEEDER_ACTIVE_LOW_DELAY_MS        100UL
#define FEEDER_MIN_DURATION_MS            250UL
#define FEEDER_MAX_DURATION_MS            10000UL

// ============================================================================
// FAN CONTROLLER CONFIGURATION
// ============================================================================

#define FAN_CONTROL_INTERVAL_MS           2000UL
#define FAN_DEFAULT_ENABLED               0
#define FAN_DEFAULT_AUTO_MODE             1
#define FAN_DEFAULT_MANUAL_STATE          0
#define FAN_DEFAULT_TEMP_ON_C             32.0f
#define FAN_DEFAULT_TEMP_OFF_C            30.0f
#define FAN_DEFAULT_HUMIDITY_ON_PCT       80.0f
#define FAN_DEFAULT_HUMIDITY_OFF_PCT      75.0f

// ============================================================================
// AUTOMATOR CONFIGURATION
// ============================================================================

/**
 * @section Automation Settings
 * @brief ตั้งค่าพื้นฐานสำหรับระบบควบคุมอัตโนมัติ
 */
#define DOSING_PUMP_RATED_VOLTAGE_V         12.0f   // สเปกปั๊มโดส: DC 12V
#define DOSING_PUMP_RATED_CURRENT_A         0.25f   // สเปกปั๊มโดส: กระแสประมาณ 0.25A
#define DOSING_PUMP_FLOW_RATE_ML_PER_MIN    39.0f   // สเปกปั๊มโดส: อัตราการไหล 39 mL/min
#define AUTOMATOR_DOSE_A_VOLUME_ML          1.5f    // ปริมาตรสาร A ต่อรอบ (เริ่มต้น 1:1 กับสาร B)
#define AUTOMATOR_DOSE_B_VOLUME_ML          1.5f    // ปริมาตรสาร B ต่อรอบ (เริ่มต้น 1:1 กับสาร A)
#define AUTOMATOR_DOSE_VOLUME_ML            AUTOMATOR_DOSE_A_VOLUME_ML // legacy alias
#define HW_TEST_PUMP_TEST_VOLUME_ML         2.0f    // ทดสอบปั๊มครั้งละประมาณ 2.0 mL
#define AUTOMATOR_CHECK_INTERVAL    5000    // ตรวจสอบสถานะทุก 5 วินาที
#define AUTOMATOR_DOSE_A_MS         ((unsigned long)((AUTOMATOR_DOSE_A_VOLUME_ML * 60000.0f / DOSING_PUMP_FLOW_RATE_ML_PER_MIN) + 0.5f))
#define AUTOMATOR_DOSE_B_MS         ((unsigned long)((AUTOMATOR_DOSE_B_VOLUME_ML * 60000.0f / DOSING_PUMP_FLOW_RATE_ML_PER_MIN) + 0.5f))
#define AUTOMATOR_PUMP_DOSE_MS      AUTOMATOR_DOSE_A_MS
#define AUTOMATOR_MIX_AFTER_A_MS    (10UL * 60UL * 1000UL) // รอให้สาร A กระจายตัวก่อนจ่ายสาร B
#define HW_TEST_PUMP_DURATION_MS    ((unsigned long)((HW_TEST_PUMP_TEST_VOLUME_ML * 60000.0f / DOSING_PUMP_FLOW_RATE_ML_PER_MIN) + 0.5f))
#define AUTOMATOR_POST_DOSE_MIX_MS  (20UL * 60UL * 1000UL) // รอให้สาร A+B เข้ากันก่อนวัด TDS ใหม่
#define AUTOMATOR_COOLDOWN_MS       AUTOMATOR_POST_DOSE_MIX_MS
#define AUTOMATOR_DEFAULT_TDS       800.0f  // ค่า TDS พื้นฐาน
#define AUTOMATOR_TDS_HYSTERESIS_PPM 30.0f  // เริ่มจ่ายเมื่อ TDS ต่ำกว่าเป้าหมายเกิน deadband นี้

// ============================================================================
// WATER SYSTEM CONFIGURATION
// ============================================================================

#define WATER_LEVEL_TRIGGER_STATE      LOW
#define OVERFLOW_SENSOR_TRIGGER_STATE  LOW
#define WATER_CIRCULATION_DEFAULT_ENABLED 1
#define WATER_REFILL_DEFAULT_ENABLED      0
#define WATER_REFILL_MAX_RUNTIME_MS       120000UL
#define WATER_REFILL_MIN_INTERVAL_MS      300000UL
#define WATER_REFILL_ROUTE_DEFAULT        2
#define WATER_ALLOW_DIRECT_SUMP_REFILL_DEFAULT 0
#define WATER_FISH_REFILL_INTERVAL_MS     604800000UL
#define WATER_FISH_REFILL_MAX_RUNTIME_MS  30000UL
#define WATER_DIRECT_SUMP_FALLBACK_DELAY_MS    30000UL

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
