/**
 * @file logger.h
 * @brief Stub logger for native unit testing
 */

#ifndef LOGGER_H_STUB
#define LOGGER_H_STUB

#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#define LOG_ERROR(...) ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_INFO(...)  ((void)0)
#define LOG_DEBUG(...) ((void)0)

#endif
