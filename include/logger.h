/**
 * @file logger.h
 * @brief Production Logging System with Conditional Debug
 * @details Structured logging with log levels and optional debug output
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <WebSerial.h>

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) { Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__); WebSerial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__); }
#else
#define LOG_ERROR(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) { Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__); WebSerial.printf("[WARN] " fmt "\n", ##__VA_ARGS__); }
#else
#define LOG_WARN(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) { Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__); WebSerial.printf("[INFO] " fmt "\n", ##__VA_ARGS__); }
#else
#define LOG_INFO(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) { Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); WebSerial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); }
#else
#define LOG_DEBUG(fmt, ...)
#endif

// ============================================================================
// UTILITY MACROS
// ============================================================================

#define LOG_MODULE_START(module) LOG_INFO("=== %s ===", module)
#define LOG_MODULE_END(module) LOG_INFO("===========")

#endif // LOGGER_H
