/**
 * @file i2cBus.cpp
 * @brief Shared I2C bus setup and locking helpers.
 */

#include "i2cBus.h"

#ifndef NATIVE_TEST
#include "config.h"
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static SemaphoreHandle_t _i2cMutex = NULL;
static bool _i2cStarted = false;

static void _ensureMutex(void) {
    if (_i2cMutex == NULL) {
        _i2cMutex = xSemaphoreCreateMutex();
    }
}

void i2cBusSetup(void) {
    _ensureMutex();

    if (!_i2cStarted) {
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        Wire.setClock(I2C_CLOCK_HZ);
        _i2cStarted = true;
    }
}

bool i2cBusLock(uint32_t timeoutMs) {
    _ensureMutex();
    if (_i2cMutex == NULL) {
        return true;
    }

    TickType_t waitTicks = 0;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        waitTicks = pdMS_TO_TICKS(timeoutMs);
    }

    return xSemaphoreTake(_i2cMutex, waitTicks) == pdTRUE;
}

void i2cBusUnlock(void) {
    if (_i2cMutex != NULL) {
        xSemaphoreGive(_i2cMutex);
    }
}
#endif
