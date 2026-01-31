/**
 * @file commandHandler.cpp
 * @brief Command Handler Implementation
 * @details รวมคำสั่งทั้งหมดไว้ที่เดียว ใช้ได้ทั้ง Serial และ Telnet
 */

#include "commandHandler.h"
#include "config.h"
#include "logger.h"
#include "system.h"
#include "wifiConn.h"
#include "netpie.h"
#include "phSensor.h"
#include "TdsSensor.h"
#include "dhtSensor.h"
#include "tempSensor.h"
#include "lightSensor.h"
#include "lightController.h"
#include "telnetServer.h"
#include <WiFi.h>

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static char _cmdBuffer[64];
static size_t _cmdIndex = 0;

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief ส่งข้อความไปยังเป้าหมายที่กำหนด
 */
void commandPrintf(CommandOutput_t output, const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    
    if (output == CMD_OUTPUT_SERIAL || output == CMD_OUTPUT_BOTH) {
        Serial.print(buf);
    }
    if (output == CMD_OUTPUT_TELNET || output == CMD_OUTPUT_BOTH) {
        telnetPrintf("%s", buf);
    }
}

/**
 * @brief แสดงรายการคำสั่งทั้งหมด
 */
static void _showHelp(CommandOutput_t out) {
    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== AVAILABLE COMMANDS ==========\r\n");
    commandPrintf(out, "  help     - แสดงรายการคำสั่ง\r\n");
    commandPrintf(out, "  clear    - ล้างหน้าจอ\r\n");
    commandPrintf(out, "  status   - แสดงค่าเซ็นเซอร์ทั้งหมด\r\n");
    commandPrintf(out, "  health   - แสดงสุขภาพระบบ\r\n");
    commandPrintf(out, "  wifi     - แสดงข้อมูล WiFi\r\n");
    commandPrintf(out, "  mqtt     - แสดงสถานะ NETPIE\r\n");
    commandPrintf(out, "  ph       - อ่านค่า pH ปัจจุบัน\r\n");
    commandPrintf(out, "  cal7     - Calibrate pH 7.0\r\n");
    commandPrintf(out, "  cal4     - Calibrate pH 4.0\r\n");
    commandPrintf(out, "  light on - เปิดไฟปลูกพืช\r\n");
    commandPrintf(out, "  light off- ปิดไฟปลูกพืช\r\n");
    commandPrintf(out, "  version  - แสดง Firmware Version\r\n");
    commandPrintf(out, "  reboot   - รีสตาร์ทบอร์ด\r\n");
    commandPrintf(out, "  reset    - Factory Reset (ล้าง WiFi)\r\n");
    commandPrintf(out, "=========================================\r\n");
}

/**
 * @brief แสดงค่าเซ็นเซอร์ทั้งหมด
 */
static void _showStatus(CommandOutput_t out) {
    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== SENSOR STATUS ==========\r\n");
    commandPrintf(out, "  Water Temp : %.2f C\r\n", tempRead());
    commandPrintf(out, "  Air Temp   : %.2f C\r\n", dhtReadTemperature());
    commandPrintf(out, "  Humidity   : %.2f %%\r\n", dhtReadHumidity());
    commandPrintf(out, "  TDS        : %.0f ppm\r\n", tdsRead(tempRead()));
    commandPrintf(out, "  pH         : %.2f\r\n", phRead());
    commandPrintf(out, "  Light      : %.0f lux\r\n", lightRead());
    commandPrintf(out, "===================================\r\n");
}

/**
 * @brief แสดงสุขภาพระบบ
 */
static void _showHealth(CommandOutput_t out) {
    SystemHealth_t health;
    systemGetHealth(&health);
    
    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== SYSTEM HEALTH ==========\r\n");
    commandPrintf(out, "  Uptime         : %lu s\r\n", health.uptimeMs / 1000);
    commandPrintf(out, "  Free Heap      : %lu B\r\n", health.freeHeap);
    commandPrintf(out, "  Min Free Heap  : %lu B\r\n", health.minFreeHeap);
    commandPrintf(out, "  Watchdog Resets: %u\r\n", health.watchdogResets);
    commandPrintf(out, "  WiFi Reconnects: %u\r\n", health.wifiReconnects);
    commandPrintf(out, "===================================\r\n");
}

/**
 * @brief แสดงข้อมูล WiFi
 */
static void _showWifi(CommandOutput_t out) {
    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== WIFI INFO ==========\r\n");
    if (wifiIsConnected()) {
        commandPrintf(out, "  Status   : Connected\r\n");
        commandPrintf(out, "  SSID     : %s\r\n", WiFi.SSID().c_str());
        commandPrintf(out, "  IP       : %s\r\n", WiFi.localIP().toString().c_str());
        commandPrintf(out, "  RSSI     : %d dBm\r\n", WiFi.RSSI());
        commandPrintf(out, "  Hostname : %s\r\n", OTA_HOSTNAME);
    } else {
        commandPrintf(out, "  Status   : Disconnected\r\n");
    }
    commandPrintf(out, "===============================\r\n");
}

/**
 * @brief แสดงสถานะ MQTT
 */
static void _showMqtt(CommandOutput_t out) {
    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== MQTT STATUS ==========\r\n");
    if (netpieIsConnected()) {
        commandPrintf(out, "  Status : Connected to NETPIE\r\n");
    } else {
        commandPrintf(out, "  Status : Disconnected\r\n");
    }
    commandPrintf(out, "  Broker : %s:%d\r\n", MQTT_BROKER, MQTT_PORT);
    commandPrintf(out, "=================================\r\n");
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void commandSetup(void) {
    memset(_cmdBuffer, 0, sizeof(_cmdBuffer));
    _cmdIndex = 0;
    LOG_INFO("[CMD] Command handler initialized");
}

void commandProcess(char* cmd, CommandOutput_t output) {
    // Trim whitespace
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\r' || cmd[len-1] == '\n')) {
        cmd[--len] = '\0';
    }
    
    // Skip empty commands
    if (len == 0) return;
    
    LOG_DEBUG("[CMD] Processing: %s", cmd);
    
    // === Command Parsing ===
    
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        _showHelp(output);
    }
    else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        // ANSI escape code to clear screen and move cursor to home
        commandPrintf(output, "\033[2J\033[H");
    }
    else if (strcmp(cmd, "status") == 0) {
        _showStatus(output);
    }
    else if (strcmp(cmd, "health") == 0) {
        _showHealth(output);
    }
    else if (strcmp(cmd, "wifi") == 0) {
        _showWifi(output);
    }
    else if (strcmp(cmd, "mqtt") == 0) {
        _showMqtt(output);
    }
    else if (strcmp(cmd, "ph") == 0) {
        commandPrintf(output, "[PH] pH: %.2f, Voltage: %.1f mV\r\n", phRead(), phReadVoltage());
    }
    else if (strcmp(cmd, "cal7") == 0) {
        commandPrintf(output, "[PH] Calibrating pH 7.0...\r\n");
        phCalibratePh7();
        commandPrintf(output, "[PH] Calibration complete!\r\n");
    }
    else if (strcmp(cmd, "cal4") == 0) {
        commandPrintf(output, "[PH] Calibrating pH 4.0...\r\n");
        phCalibratePh4();
        commandPrintf(output, "[PH] Calibration complete!\r\n");
    }
    else if (strcmp(cmd, "light on") == 0) {
        lightCtrlSetState(true);
        commandPrintf(output, "[LIGHT] Forced ON\r\n");
    }
    else if (strcmp(cmd, "light off") == 0) {
        lightCtrlSetState(false);
        commandPrintf(output, "[LIGHT] Forced OFF\r\n");
    }
    else if (strcmp(cmd, "light auto") == 0) {
        lightCtrlSetEnabled(1);  // Re-enable schedule
        commandPrintf(output, "[LIGHT] Auto mode (schedule enabled)\r\n");
    }
    else if (strcmp(cmd, "version") == 0) {
        commandPrintf(output, "Firmware: %s\r\n", systemGetVersion());
        commandPrintf(output, "Build: %s %s\r\n", __DATE__, __TIME__);
    }
    else if (strcmp(cmd, "reboot") == 0) {
        commandPrintf(output, "[SYS] Rebooting...\r\n");
        Serial.flush();  // ให้ข้อความส่งออกก่อน (non-blocking)
        ESP.restart();
    }
    else if (strcmp(cmd, "reset") == 0) {
        commandPrintf(output, "[SYS] Factory Reset...\r\n");
        Serial.flush();  // ให้ข้อความส่งออกก่อน (non-blocking)
        systemFactoryReset();
    }
    else {
        commandPrintf(output, "[CMD] Unknown: %s (type 'help')\r\n", cmd);
    }
}

void commandCheckSerial(void) {
    while (Serial.available()) {
        char c = Serial.read();
        
        if (c == '\n' || c == '\r') {
            if (_cmdIndex > 0) {
                _cmdBuffer[_cmdIndex] = '\0';
                commandProcess(_cmdBuffer, CMD_OUTPUT_SERIAL);
                _cmdIndex = 0;
            }
        } else if (_cmdIndex < sizeof(_cmdBuffer) - 1) {
            _cmdBuffer[_cmdIndex++] = c;
        }
    }
}
