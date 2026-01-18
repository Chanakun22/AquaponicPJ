/**
 * @file ota.h
 * @brief OTA (Over-The-Air) Update Support
 */

#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief Initialize OTA update handler
 */
void otaSetup(void);

/**
 * @brief OTA loop (call in main loop)
 */
void otaLoop(void);

/**
 * @brief Check if OTA is enabled
 */
bool otaIsEnabled(void);

#endif // OTA_H
