/**
 * @file ota.cpp
 * @brief OTA Update Implementation
 */

#include "ota.h"
#include "logger.h"
#include "config.h"

#if OTA_ENABLED == 1
#include <ArduinoOTA.h>
#include <WiFi.h>
#if defined(ESP32) && WATCHDOG_ENABLED
#include "esp_task_wdt.h"
#endif
#endif

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static bool _otaInitialized = false;
static bool _otaInProgress = false;

static bool otaPasswordConfigured(void) {
    return strlen(OTA_PASSWORD) > 0 && strcmp(OTA_PASSWORD, UNCONFIGURED_SECRET_SENTINEL) != 0;
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void otaSetup(void) {
    #if OTA_ENABLED == 1
    if (_otaInitialized) {
        return;
    }

    if (!otaPasswordConfigured()) {
        LOG_WARN("OTA disabled: SECRET_OTA_PASSWORD is not configured");
        return;
    }
    
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    
    ArduinoOTA.onStart([]() {
        _otaInProgress = true;
        LOG_WARN("OTA Update started");
    });
    
    ArduinoOTA.onEnd([]() {
        _otaInProgress = false;
        LOG_INFO("OTA Update finished");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int percent = (total > 0U)
            ? (unsigned int)(((unsigned long long)progress * 100ULL) / (unsigned long long)total)
            : 0U;
        static unsigned int lastPercent = 0;

        #if defined(ESP32) && WATCHDOG_ENABLED
        esp_task_wdt_reset();
        #endif
        
        if (percent != lastPercent && percent % 10 == 0) {
            LOG_INFO("OTA Progress: %u%%", percent);
            lastPercent = percent;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        _otaInProgress = false;
        LOG_ERROR("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            LOG_ERROR("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            LOG_ERROR("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            LOG_ERROR("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            LOG_ERROR("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            LOG_ERROR("End Failed");
        }
    });
    
    ArduinoOTA.begin();
    _otaInitialized = true;
    
    LOG_INFO("OTA enabled - Hostname: %s", OTA_HOSTNAME);
    #else
    LOG_DEBUG("OTA disabled in config");
    #endif
}

void otaLoop(void) {
    #if OTA_ENABLED == 1
    if (_otaInitialized) {
        ArduinoOTA.handle();
        #if defined(ESP32) && WATCHDOG_ENABLED
        if (_otaInProgress) {
            esp_task_wdt_reset();
        }
        #endif
    }
    #endif
}

bool otaIsEnabled(void) {
    #if OTA_ENABLED == 1
    return _otaInitialized;
    #else
    return false;
    #endif 

    
}
