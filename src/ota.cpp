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
#endif

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static bool _otaInitialized = false;

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void otaSetup(void) {
    #if OTA_ENABLED == 1
    if (_otaInitialized) {
        return;
    }
    
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    
    if (strlen(OTA_PASSWORD) > 0) {
        ArduinoOTA.setPassword(OTA_PASSWORD);
    }
    
    ArduinoOTA.onStart([]() {
        LOG_WARN("OTA Update started");
    });
    
    ArduinoOTA.onEnd([]() {
        LOG_INFO("OTA Update finished");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int percent = (progress / (total / 100));
        static unsigned int lastPercent = 0;
        
        if (percent != lastPercent && percent % 10 == 0) {
            LOG_INFO("OTA Progress: %u%%", percent);
            lastPercent = percent;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
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
