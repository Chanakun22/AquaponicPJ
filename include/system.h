/**
 * @file system.h
 * @brief System Management - Health Monitoring, Factory Reset, Version
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <Arduino.h>

// ============================================================================
// VERSION INFORMATION
// ============================================================================

#define FIRMWARE_VERSION_MAJOR 2
#define FIRMWARE_VERSION_MINOR 3
#define FIRMWARE_VERSION_PATCH 0
#define FIRMWARE_VERSION_STRING "2.3.0"

// ============================================================================
// SYSTEM HEALTH MONITORING
// ============================================================================

/**
 * @brief System health status
 */
typedef struct {
    unsigned long uptimeMs;        // System uptime in milliseconds
    unsigned long freeHeap;        // Free heap memory (bytes)
    unsigned long heapSize;        // Total heap memory (bytes)
    unsigned long minFreeHeap;     // Minimum free heap since boot
    float cpuTemp;                 // Chip temperature (Celsius)
    unsigned int watchdogResets;   // Number of watchdog resets
    unsigned int wifiReconnects;   // Number of WiFi reconnections
    unsigned int mqttReconnects;   // Number of MQTT reconnections
    char resetReason[32];          // Reason for the last reset
    bool sensorsOk;                // All sensors operational
} SystemHealth_t;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief Initialize system management
 */
void systemInit(void);

/**
 * @brief Get human readable reset reason
 * @return String description of reset reason
 */
String systemGetResetReasonString(void);



/**
 * @brief System loop (call in main loop)
 */
void systemLoop(void);

/**
 * @brief Get system health status
 */
void systemGetHealth(SystemHealth_t* health);

/**
 * @brief Factory reset - clear all settings
 */
void systemFactoryReset(void);

/**
 * @brief Get firmware version string
 */
const char* systemGetVersion(void);

/**
 * @brief Get system uptime in seconds
 */
unsigned long systemGetUptimeSeconds(void);

/**
 * @brief Check if system is healthy
 */
bool systemIsHealthy(void);

/**
 * @brief Increment watchdog reset counter
 */
void systemIncrementWatchdogResets(void);

/**
 * @brief Increment WiFi reconnect counter
 */
void systemIncrementWifiReconnects(void);

/**
 * @brief Increment MQTT reconnect counter
 */
void systemIncrementMqttReconnects(void);

#endif // SYSTEM_H
