/**
 * @file logger.h
 * @brief Production Logging System with Conditional Debug
 * @details Structured logging with log levels and optional debug output
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// ============================================================================
// LOG LEVEL CONFIGURATION
// ============================================================================

/**
 * @brief Log levels (lower number = higher priority)
 */
#define LOG_LEVEL_NONE    0
#define LOG_LEVEL_ERROR   1
#define LOG_LEVEL_WARN    2
#define LOG_LEVEL_INFO    3
#define LOG_LEVEL_DEBUG   4

/**
 * @brief Current log level (set in config.h)
 * Production: LOG_LEVEL_INFO (no debug)
 * Development: LOG_LEVEL_DEBUG (all logs)
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

// ============================================================================
// LOG MACROS
// ============================================================================

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

// ============================================================================
// UTILITY MACROS
// ============================================================================

#define LOG_MODULE_START(module) LOG_INFO("=== %s ===", module)
#define LOG_MODULE_END(module) LOG_INFO("===========")

#endif // LOGGER_H
