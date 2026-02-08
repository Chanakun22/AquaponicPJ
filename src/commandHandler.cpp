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
#include "localMqtt.h"
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
    commandPrintf(out, "  test     - รันระบบ Self-Test\r\n");
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
// SYSTEM TEST
// ============================================================================

static void _runSystemTest(CommandOutput_t out) {
    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== SYSTEM DIAGNOSTIC ==========\r\n");
    
    // 1. Connectivity
    commandPrintf(out, "[NET] WiFi       : %s\r\n", wifiIsConnected() ? "CONNECTED" : "FAIL");
    if (wifiIsConnected()) {
        commandPrintf(out, "      IP         : %s\r\n", WiFi.localIP().toString().c_str());
        commandPrintf(out, "      RSSI       : %d dBm\r\n", WiFi.RSSI());
    }
    
    commandPrintf(out, "[NET] NETPIE     : %s\r\n", netpieIsConnected() ? "CONNECTED" : "FAIL");
    commandPrintf(out, "[NET] Local MQTT : %s\r\n", localMqttIsConnected() ? "CONNECTED" : "FAIL");
    
    // 2. Sensors
    float t_water = tempRead();
    float t_air = dhtReadTemperature();
    float humid = dhtReadHumidity();
    float tds = tdsRead(t_water);
    float light = lightRead();
    float ph = phRead();
    
    commandPrintf(out, "[SEN] Water Temp : %.2f C  %s\r\n", t_water, isnan(t_water) ? "(FAIL)" : "(OK)");
    commandPrintf(out, "[SEN] Air Temp   : %.2f C  %s\r\n", t_air, isnan(t_air) ? "(FAIL)" : "(OK)");
    commandPrintf(out, "[SEN] Humidity   : %.2f %%  %s\r\n", humid, isnan(humid) ? "(FAIL)" : "(OK)");
    commandPrintf(out, "[SEN] TDS        : %.0f ppm %s\r\n", tds, (tds < 0) ? "(FAIL)" : "(OK)");
    commandPrintf(out, "[SEN] Light      : %.0f lux %s\r\n", light, (light < 0) ? "(FAIL)" : "(OK)");
    commandPrintf(out, "[SEN] pH         : %.2f    %s\r\n", ph, (ph < 0) ? "(FAIL)" : "(OK)");
    
    // 3. System Health
    SystemHealth_t health;
    systemGetHealth(&health);
    
    commandPrintf(out, "[SYS] Uptime     : %lu s\r\n", health.uptimeMs / 1000);
    commandPrintf(out, "[SYS] Free Heap  : %lu / %lu B (Min: %lu)\r\n", health.freeHeap, health.heapSize, health.minFreeHeap);
    commandPrintf(out, "[SYS] CPU Temp   : %.1f C\r\n", health.cpuTemp);
    
    // Watchdog & Resets
    commandPrintf(out, "[WDT] Resets     : %u %s\r\n", health.watchdogResets, (health.watchdogResets > 0) ? "(WARNING)" : "(OK)");
    commandPrintf(out, "[SYS] Reset Reason: %s\r\n", health.resetReason);
    commandPrintf(out, "[SYS] Reconnects : WiFi: %u, MQTT: %u\r\n", health.wifiReconnects, health.mqttReconnects);
    
    // Time Sync
    struct tm timeinfo;
    bool timeOk = getLocalTime(&timeinfo, 100); // 100ms timeout
    if(timeOk) {
        char timeStr[30];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        commandPrintf(out, "[CLK] Local Time : %s (OK)\r\n", timeStr);
    } else {
         commandPrintf(out, "[CLK] Local Time : UNSYNCED (FAIL)\r\n");
    }

    commandPrintf(out, "=======================================\r\n");
    commandPrintf(out, "TEST COMPLETE.\r\n");
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
    // Trim whitespace (Right side)
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\r' || cmd[len-1] == '\n')) {
        cmd[--len] = '\0';
    }
    
    // Trim whitespace (Left side) -> Shift pointer? No, just skip in comparison or memmove
    // Simple implementation: skip leading spaces
    char* cleanCmd = cmd;
    while (*cleanCmd == ' ') cleanCmd++;
    
    // Skip empty commands
    if (strlen(cleanCmd) == 0) return;
    
    LOG_DEBUG("[CMD] Processing: %s", cleanCmd);
    
    // === Command Parsing using strncmp/strcmp ===
    
    if (strcmp(cleanCmd, "help") == 0 || strcmp(cleanCmd, "?") == 0) {
        _showHelp(output);
    }
    else if (strcmp(cleanCmd, "clear") == 0 || strcmp(cleanCmd, "cls") == 0) {
        // ANSI escape code to clear screen and move cursor to home
        commandPrintf(output, "\033[2J\033[H");
    }
    else if (strcmp(cleanCmd, "status") == 0) {
        _showStatus(output);
    }
    else if (strcmp(cleanCmd, "test") == 0) {
        _runSystemTest(output);
    }
    else if (strcmp(cleanCmd, "health") == 0) {
        _showHealth(output);
    }
    else if (strcmp(cleanCmd, "wifi") == 0) {
        _showWifi(output);
    }
    else if (strcmp(cleanCmd, "mqtt") == 0) {
        _showMqtt(output);
    }
    else if (strcmp(cleanCmd, "ph") == 0) {
        commandPrintf(output, "[PH] pH: %.2f, Voltage: %.1f mV\r\n", phRead(), phReadVoltage());
    }
    else if (strcmp(cleanCmd, "cal7") == 0) {
        commandPrintf(output, "[PH] Calibrating pH 7.0...\r\n");
        phCalibratePh7();
        commandPrintf(output, "[PH] Calibration complete!\r\n");
    }
    else if (strcmp(cleanCmd, "cal4") == 0) {
        commandPrintf(output, "[PH] Calibrating pH 4.0...\r\n");
        phCalibratePh4();
        commandPrintf(output, "[PH] Calibration complete!\r\n");
    }
    else if (strcmp(cleanCmd, "light on") == 0) {
        lightCtrlSetState(true);
        commandPrintf(output, "[LIGHT] Forced ON\r\n");
    }
    else if (strcmp(cleanCmd, "light off") == 0) {
        lightCtrlSetState(false);
        commandPrintf(output, "[LIGHT] Forced OFF\r\n");
    }
    else if (strcmp(cleanCmd, "light auto") == 0) {
        lightCtrlSetEnabled(1);  // Re-enable schedule
        commandPrintf(output, "[LIGHT] Auto mode (schedule enabled)\r\n");
    }
    else if (strcmp(cleanCmd, "version") == 0) {
        commandPrintf(output, "Firmware: %s\r\n", systemGetVersion());
        commandPrintf(output, "Build: %s %s\r\n", __DATE__, __TIME__);
    }
    else if (strcmp(cleanCmd, "reboot") == 0) {
        commandPrintf(output, "[SYS] Rebooting...\r\n");
        Serial.flush();
        ESP.restart();
    }
    else if (strcmp(cleanCmd, "reset") == 0) {
        commandPrintf(output, "[SYS] Factory Reset...\r\n");
        Serial.flush();
        systemFactoryReset();
    }
    else {
        commandPrintf(output, "[CMD] Unknown: %s (type 'help')\r\n", cleanCmd);
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
