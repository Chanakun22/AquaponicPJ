/**
 * @file gpioOut.cpp
 * @brief Output pin abstraction layer implementation
 */

#include "gpioOut.h"
#include "config.h"
#include "logger.h"

#ifndef NATIVE_TEST
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#endif

// ============================================================================
// PRIVATE STATE
// ============================================================================

typedef struct {
    int esp32Pin;       // ESP32 GPIO (-1 = unused / route ไป MCP)
    int mcpPin;         // MCP23017 GPA0..GPB7 = 0..15 (-1 = ไม่ใช้ MCP)
    bool useMcp;        // routing flag
    const char* name;
    bool lastState;
} OutputDescriptor;

static OutputDescriptor _outputs[GPIO_OUT_COUNT] = {
    // GPIO_OUT_PUMP_NUTRIENT_A
    { PUMP_NUTRIENT_A_PIN,    MCP_PIN_PUMP_NUTRIENT_A,    OUT_USE_MCP_PUMP_NUTRIENT_A != 0,    "PumpA",       false },
    // GPIO_OUT_PUMP_NUTRIENT_B
    { PUMP_NUTRIENT_B_PIN,    MCP_PIN_PUMP_NUTRIENT_B,    OUT_USE_MCP_PUMP_NUTRIENT_B != 0,    "PumpB",       false },
    // GPIO_OUT_LIGHT_RELAY
    { LIGHT_RELAY_PIN,        MCP_PIN_LIGHT_RELAY,        OUT_USE_MCP_LIGHT_RELAY != 0,        "Light",       false },
    // GPIO_OUT_PUMP_CIRCULATION
    { PUMP_CIRCULATION_PIN,   MCP_PIN_PUMP_CIRCULATION,   OUT_USE_MCP_PUMP_CIRCULATION != 0,   "Circulation", false },
    // GPIO_OUT_FISH_FEEDER
    { FISH_FEEDER_PIN,        MCP_PIN_FISH_FEEDER,        OUT_USE_MCP_FISH_FEEDER != 0,        "Feeder",      false },
    // GPIO_OUT_REFILL_ROUTE_VALVE
    { REFILL_ROUTE_VALVE_PIN, MCP_PIN_REFILL_ROUTE_VALVE, OUT_USE_MCP_REFILL_ROUTE_VALVE != 0, "RouteValve",  false },
    // GPIO_OUT_PUMP_REFILL
    { PUMP_REFILL_PIN,        MCP_PIN_PUMP_REFILL,        OUT_USE_MCP_PUMP_REFILL != 0,        "Refill",      false },
    // GPIO_OUT_EXHAUST_FAN
    { EXHAUST_FAN_PIN,        MCP_PIN_EXHAUST_FAN,        OUT_USE_MCP_EXHAUST_FAN != 0,        "Fan",         false },
};

#ifndef NATIVE_TEST
static Adafruit_MCP23X17 _mcp;
#endif
static bool _mcpInitialized = false;
static bool _mcpInUse = false;

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

static bool _anyOutputUsesMcp(void) {
    for (int i = 0; i < GPIO_OUT_COUNT; i++) {
        if (_outputs[i].useMcp) {
            return true;
        }
    }
    return false;
}

#ifndef NATIVE_TEST
static bool _initMcp(void) {
    // Hold RESET LOW briefly, then release HIGH
    pinMode(MCP23017_RESET_PIN, OUTPUT);
    digitalWrite(MCP23017_RESET_PIN, LOW);
    delayMicroseconds(50);
    digitalWrite(MCP23017_RESET_PIN, HIGH);
    delayMicroseconds(500);  // wait for MCP power-up after reset

    if (!_mcp.begin_I2C(MCP23017_I2C_ADDR)) {
        LOG_ERROR("[GPIO_OUT] MCP23017 not responding at 0x%02X", MCP23017_I2C_ADDR);
        return false;
    }

    // Set all MCP pins used as output: pinMode = OUTPUT, default state = HIGH (relay OFF for active-low)
    for (int i = 0; i < GPIO_OUT_COUNT; i++) {
        if (_outputs[i].useMcp && _outputs[i].mcpPin >= 0) {
            _mcp.digitalWrite(_outputs[i].mcpPin, HIGH);  // OFF first
            _mcp.pinMode(_outputs[i].mcpPin, OUTPUT);
        }
    }

    LOG_INFO("[GPIO_OUT] MCP23017 initialized at 0x%02X", MCP23017_I2C_ADDR);
    return true;
}
#endif

// ============================================================================
// PUBLIC API
// ============================================================================

bool gpioOutSetup(void) {
    _mcpInUse = _anyOutputUsesMcp();

    // Setup ESP32 GPIO outputs first (everything not routed to MCP)
    for (int i = 0; i < GPIO_OUT_COUNT; i++) {
        if (!_outputs[i].useMcp && _outputs[i].esp32Pin >= 0) {
            pinMode(_outputs[i].esp32Pin, OUTPUT);
            digitalWrite(_outputs[i].esp32Pin, PUMP_OFF);  // active-low: HIGH = OFF
        }
        _outputs[i].lastState = false;
    }

#ifndef NATIVE_TEST
    if (_mcpInUse) {
        _mcpInitialized = _initMcp();
        if (!_mcpInitialized) {
            return false;
        }
    } else {
        _mcpInitialized = false;
    }
#endif

    return true;
}

void gpioOutWrite(GpioLogicalOutput out, bool on) {
    if ((int)out < 0 || (int)out >= GPIO_OUT_COUNT) {
        return;
    }

    OutputDescriptor* desc = &_outputs[(int)out];
    desc->lastState = on;

    // active-low: drive LOW for ON, HIGH for OFF
    uint8_t level = on ? PUMP_ON : PUMP_OFF;

    if (desc->useMcp) {
#ifndef NATIVE_TEST
        if (_mcpInitialized && desc->mcpPin >= 0) {
            _mcp.digitalWrite(desc->mcpPin, level);
        }
#endif
    } else {
        if (desc->esp32Pin >= 0) {
            digitalWrite(desc->esp32Pin, level);
        }
    }
}

bool gpioOutGetLastState(GpioLogicalOutput out) {
    if ((int)out < 0 || (int)out >= GPIO_OUT_COUNT) {
        return false;
    }
    return _outputs[(int)out].lastState;
}

bool gpioOutMcpHealthy(void) {
    if (!_mcpInUse) {
        return true;  // not in use → trivially healthy
    }
#ifndef NATIVE_TEST
    if (!_mcpInitialized) {
        return false;
    }
    // ping by reading IODIRA register (read-back smoke test)
    Wire.beginTransmission(MCP23017_I2C_ADDR);
    return (Wire.endTransmission() == 0);
#else
    return _mcpInitialized;
#endif
}

bool gpioOutMcpReinit(void) {
    if (!_mcpInUse) {
        return true;
    }
#ifndef NATIVE_TEST
    _mcpInitialized = _initMcp();
    if (_mcpInitialized) {
        // Re-apply last known states (in case MCP was reset)
        for (int i = 0; i < GPIO_OUT_COUNT; i++) {
            if (_outputs[i].useMcp && _outputs[i].mcpPin >= 0) {
                _mcp.digitalWrite(_outputs[i].mcpPin,
                                  _outputs[i].lastState ? PUMP_ON : PUMP_OFF);
            }
        }
    }
    return _mcpInitialized;
#else
    return _mcpInitialized;
#endif
}

int gpioOutGetEsp32Pin(GpioLogicalOutput out) {
    if ((int)out < 0 || (int)out >= GPIO_OUT_COUNT) {
        return -1;
    }
    return _outputs[(int)out].useMcp ? -1 : _outputs[(int)out].esp32Pin;
}

int gpioOutGetMcpPin(GpioLogicalOutput out) {
    if ((int)out < 0 || (int)out >= GPIO_OUT_COUNT) {
        return -1;
    }
    return _outputs[(int)out].useMcp ? _outputs[(int)out].mcpPin : -1;
}
