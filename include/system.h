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
#define FIRMWARE_VERSION_MINOR 5
#define FIRMWARE_VERSION_PATCH 0
#define FIRMWARE_VERSION_STRING "2.5.0"

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
const char* systemGetResetReasonString(void);



/**
 * @brief System loop (call in main loop)
 */
void systemLoop(void);

// ============================================================================
// SENSOR MANAGEMENT
// ============================================================================

typedef enum {
    SENSOR_TDS = 0,
    SENSOR_PH,
    SENSOR_WATER_TEMP,
    SENSOR_AIR_TEMP, // & Humidity (DHT)
    SENSOR_LIGHT,
    SENSOR_COUNT
} SensorId_t;

/**
 * @brief Initialize sensor enabled states from NVS
 */
void systemSensorInit(void);

/**
 * @brief Set sensor enabled state and save to NVS
 */
void systemSetSensorEnabled(SensorId_t id, bool enabled);

/**
 * @brief Set all sensor enabled states in a single NVS transaction
 * @param states Array of SENSOR_COUNT booleans
 */
void systemSetAllSensorsEnabled(bool states[SENSOR_COUNT]);

/**
 * @brief Get sensor enabled state
 */
bool systemGetSensorEnabled(SensorId_t id);

// ============================================================================
// SYSTEM HEALTH MONITORING
// ============================================================================

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

// ============================================================================
// TASK HEARTBEAT MONITOR
// ============================================================================

/** Threshold before a task is considered stuck (ms) */
#define TASK_STUCK_THRESHOLD_MS  30000

/** Task IDs for heartbeat tracking */
typedef enum {
    TASK_NETWORKING = 0,
    TASK_SENSORS,
    TASK_CONTROL,
    TASK_ID_COUNT
} TaskId_t;

/** Task names for logging */
extern const char* TASK_NAMES[TASK_ID_COUNT];

/**
 * @brief Update heartbeat timestamp for a task (call in task loop)
 */
void systemTaskHeartbeat(TaskId_t taskId);

/**
 * @brief Record the current execution stage for a task
 */
void systemSetTaskProgress(TaskId_t taskId, const char* stage);

/**
 * @brief Get the last recorded execution stage for a task
 */
const char* systemGetTaskProgress(TaskId_t taskId);

/**
 * @brief Check all task heartbeats, log warning if any is stuck
 * @return true if all tasks are alive, false if any is stuck
 */
bool systemCheckTaskHealth(void);

/**
 * @brief Print stack high water mark for all tasks
 */
void systemPrintStackInfo(void);

/**
 * @brief Set FreeRTOS task handle for stack monitoring
 */
void systemSetTaskHandle(TaskId_t taskId, TaskHandle_t handle);

/**
 * @brief Get last heartbeat age in ms for a task
 */
unsigned long systemGetTaskHeartbeatAge(TaskId_t taskId);

/**
 * @brief Get last crash info from NVS (persisted across reboot)
 * @param buf Buffer to write crash info string
 * @param bufSize Buffer size
 * @return true if there was crash info, false if clean boot
 */
bool systemGetLastCrashInfo(char* buf, size_t bufSize);

/**
 * @brief Report last crash info to Serial/logs (call once after boot)
 */
void systemReportLastCrash(void);

#endif // SYSTEM_H
