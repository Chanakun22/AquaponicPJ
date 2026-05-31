/**
 * @file config.h (mock)
 * @brief Mock config.h with all constants needed for sensor compilation
 */

#ifndef MOCK_CONFIG_H
#define MOCK_CONFIG_H

#include "Arduino.h"

// ==================== Pin Definitions ====================
#define TDS_PIN             5
#define PH_SENSOR_PIN       6
#define ONE_WIRE_PIN        13
#define DHT_PIN             15
#define I2C_SDA_PIN         8
#define I2C_SCL_PIN         9
#define LIGHT_RELAY_PIN     12
#define PUMP_CIRCULATION_PIN 16
#define PUMP_REFILL_PIN      17
#define REFILL_ROUTE_VALVE_PIN 18
#define SUMP_LEVEL_LOW_PIN   19
#define SUMP_LEVEL_HIGH_PIN  20
#define FISH_TANK_OVERFLOW_PIN 21

#define PUMP_ON              LOW
#define PUMP_OFF             HIGH
#define REFILL_ROUTE_TO_FISH_STATE PUMP_OFF
#define REFILL_ROUTE_TO_SUMP_STATE PUMP_ON

// ==================== TDS ====================
#define TDS_VREF            3.3f
#define TDS_ADC_RESOLUTION  4096.0f
#define TDS_SAMPLE_COUNT    30
#define TDS_READ_INTERVAL   1000
#define TDS_MIN             0.0f
#define TDS_MAX             2000.0f

// ==================== pH ====================
#define PH_SAMPLE_COUNT             30
#define PH_READ_INTERVAL            1000
#define PH_OVERSAMPLE_COUNT         8
#define PH_INVALID_STREAK_LIMIT     3
#define PH_VOLTAGE_FILTER_ALPHA     0.18f
#define PH_VOLTAGE_DISPLAY_FILTER_ALPHA 0.06f
#define PH_PH_FILTER_ALPHA          0.18f
#define PH_VOLTAGE_DEADBAND_MV      3.0f
#define PH_VOLTAGE_DISPLAY_DEADBAND_MV 8.0f
#define PH_PH_DEADBAND              0.01f
#define PH_PH_MAX_STEP              0.12f
#define PH_ADC_SETTLE_US            250
#define PH_ADC_DUMMY_READS          2
#define PH_VOLTAGE_AT_686           2058
#define PH_VOLTAGE_SLOPE            -59.16f
#define PH_MIN                      0.0f
#define PH_MAX                      14.0f
#define PH_CAL_POINT_401            4.01f
#define PH_CAL_POINT_686            6.86f
#define PH_CAL_POINT_918            9.18f

// ==================== DHT ====================
#define DHT_TYPE            DHT22
#define DHT_READ_INTERVAL   2000
#define DHT_SLOW_READ_WARN_MS 250
#define DHT_MAX_CONSECUTIVE_FAILURES 3
#define DHT_FAIL_BACKOFF_MS 10000

// ==================== DS18B20 ====================
#define TEMP_READ_INTERVAL  2000

// ==================== Light ====================
#define BH1750_ADDRESS      0x23
#define LIGHT_READ_INTERVAL 2000

// ==================== Serial ====================
#define SERIAL_BAUD_RATE    115200

// ==================== Logging ====================
#define LOG_LEVEL_INFO      3
#define LOG_LEVEL_DEBUG     4
#ifndef LOG_LEVEL
#define LOG_LEVEL           LOG_LEVEL_INFO
#endif

// ==================== Water System ====================
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

// ==================== Helper macros ====================
#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(s) #s

// ==================== Control source enum (needed by some headers) ====================
#include "controlSource.h"

#endif
