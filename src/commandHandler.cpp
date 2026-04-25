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
#include "fishFeeder.h"
#include "fanController.h"
#include "automator.h"
#include "waterSystem.h"
#include "telnetServer.h"
#include <WiFi.h>
#include <Wire.h>

// Pump test state (non-blocking auto-off)
static unsigned long _pumpTestStartMs = 0;
static uint8_t       _pumpTestPin     = 0;
static bool          _pumpTestActive  = false;
#define PUMP_TEST_DURATION_MS HW_TEST_PUMP_DURATION_MS

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static char _cmdBuffer[64];
static size_t _cmdIndex = 0;

static void _showLightController(CommandOutput_t out) {
    LightControlConfig cfg;
    LightControlStatus status;

    lightCtrlGetConfig(&cfg);
    lightCtrlGetStatus(&status);

    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== LIGHT CONTROL =========\r\n");
    commandPrintf(out, "  Source         : %s\r\n", lightCtrlGetCommandSourceString(cfg.commandSource));
    commandPrintf(out, "  Schedule En    : %s\r\n", cfg.enabled ? "YES" : "NO");
    commandPrintf(out, "  Manual State   : %s\r\n", cfg.manualState ? "ON" : "OFF");
    commandPrintf(out, "  Running Output : %s\r\n", status.running ? "ON" : "OFF");
    commandPrintf(out, "  NTP Synced     : %s\r\n", status.ntpSynced ? "YES" : "NO");
    commandPrintf(out, "  ON Schedule    : %d %s\r\n", cfg.onDay, cfg.onTime);
    commandPrintf(out, "  OFF Schedule   : %d %s\r\n", cfg.offDay, cfg.offTime);
    commandPrintf(out, "  Reason         : %s\r\n", status.reason);
    commandPrintf(out, "===============================\r\n");
}

static void _showFishFeeder(CommandOutput_t out) {
    FishFeederConfig cfg;
    FishFeederStatus status;

    fishFeederGetConfig(&cfg);
    fishFeederGetStatus(&status);

    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== FISH FEEDER =========\r\n");
    commandPrintf(out, "  Source         : %s\r\n", commandSourceToString(cfg.commandSource));
    commandPrintf(out, "  Enabled        : %s\r\n", cfg.enabled ? "YES" : "NO");
    commandPrintf(out, "  Feed Schedule  : %d %s\r\n", cfg.feedDay, cfg.feedTime);
    commandPrintf(out, "  Duration       : %lu ms\r\n", cfg.durationMs);
    commandPrintf(out, "  State          : %s\r\n", fishFeederGetStateString(status.state));
    commandPrintf(out, "  Running Output : %s\r\n", status.running ? "ON" : "OFF");
    commandPrintf(out, "  Last Feed      : %s\r\n", status.lastFeedAt);
    commandPrintf(out, "  Reason         : %s\r\n", status.reason);
    commandPrintf(out, "===============================\r\n");
}

static void _showWaterSystem(CommandOutput_t out) {
    WaterSystemConfig cfg;
    WaterSystemStatus status;
    waterSystemGetConfig(&cfg);
    waterSystemGetStatus(&status);

    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== WATER SYSTEM ==========\r\n");
    commandPrintf(out, "  State           : %s (%s)\r\n", waterSystemGetStateLabelTh(status.state), waterSystemGetStateString(status.state));
    commandPrintf(out, "  Reason          : %s\r\n", status.reason);
    commandPrintf(out, "  Circulation En  : %s\r\n", cfg.circulationEnabled ? "YES" : "NO");
    commandPrintf(out, "  Refill En       : %s\r\n", cfg.refillEnabled ? "YES" : "NO");
    commandPrintf(out, "  Manual Refill   : %s\r\n", cfg.manualRefill ? "YES" : "NO");
    commandPrintf(out, "  Preferred Route : %s\r\n", waterSystemGetRouteString(cfg.preferredRoute));
    commandPrintf(out, "  Allow Direct    : %s\r\n", cfg.allowDirectSumpRefill ? "YES" : "NO");
    commandPrintf(out, "  Min Refill Gap  : %lu ms\r\n", cfg.refillMinIntervalMs);
    commandPrintf(out, "  Fish Interval   : %lu ms\r\n", cfg.fishRefillIntervalMs);
    commandPrintf(out, "  Fish Max Time   : %lu ms\r\n", cfg.fishRefillMaxRuntimeMs);
    commandPrintf(out, "  Active Route    : %s\r\n", waterSystemGetRouteString(status.activeRoute));
    commandPrintf(out, "  Circ Output     : %s\r\n", status.circulationOutput ? "ON" : "OFF");
    commandPrintf(out, "  Refill Output   : %s\r\n", status.refillOutput ? "ON" : "OFF");
    commandPrintf(out, "  Route Valve Out : %s\r\n", status.routeValveOutput ? "DIRECT SUMP" : "FISH TANK");
    commandPrintf(out, "  Sump Low        : %s\r\n", status.levelLow ? "TRIGGERED" : "NORMAL");
    commandPrintf(out, "  Sump High       : %s\r\n", status.levelHigh ? "TRIGGERED" : "NORMAL");
    commandPrintf(out, "  Overflow Alarm  : %s\r\n", status.overflowAlarm ? "TRIGGERED" : "NORMAL");
    commandPrintf(out, "  Route Blocked   : %s\r\n", status.routeBlocked ? "YES" : "NO");
    commandPrintf(out, "  Fish Ready      : %s\r\n", status.fishRefillReady ? "YES" : "NO");
    commandPrintf(out, "  Fish Wait Left  : %lu ms\r\n", status.fishRefillWaitRemainingMs);
    commandPrintf(out, "  Alarm Active    : %s\r\n", status.alarmActive ? "YES" : "NO");
    commandPrintf(out, "  Max Refill Time : %lu ms\r\n", cfg.refillMaxRuntimeMs);
    commandPrintf(out, "===============================\r\n");
}

static void _showFanController(CommandOutput_t out) {
    FanControlConfig cfg;
    FanControlStatus status;
    char airBuf[16];
    char humidBuf[16];

    fanCtrlGetConfig(&cfg);
    fanCtrlGetStatus(&status);

    if (isnan(status.airTempC)) {
        snprintf(airBuf, sizeof(airBuf), "N/A");
    } else {
        snprintf(airBuf, sizeof(airBuf), "%.1f C", status.airTempC);
    }

    if (isnan(status.humidityPct)) {
        snprintf(humidBuf, sizeof(humidBuf), "N/A");
    } else {
        snprintf(humidBuf, sizeof(humidBuf), "%.1f %%", status.humidityPct);
    }

    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== FAN CONTROL =========\r\n");
    commandPrintf(out, "  State          : %s\r\n", fanCtrlGetStateString(status.state));
    commandPrintf(out, "  Mode           : %s\r\n", fanCtrlGetModeString(cfg.autoMode));
    commandPrintf(out, "  Enabled        : %s\r\n", cfg.enabled ? "YES" : "NO");
    commandPrintf(out, "  Manual State   : %s\r\n", cfg.manualState ? "ON" : "OFF");
    commandPrintf(out, "  Running Output : %s\r\n", status.running ? "ON" : "OFF");
    commandPrintf(out, "  Temp On/Off    : %.1f / %.1f C\r\n", cfg.tempOnC, cfg.tempOffC);
    commandPrintf(out, "  Humid On/Off   : %.1f / %.1f %%\r\n", cfg.humidityOnPct, cfg.humidityOffPct);
    commandPrintf(out, "  Air Temp       : %s\r\n", airBuf);
    commandPrintf(out, "  Humidity       : %s\r\n", humidBuf);
    commandPrintf(out, "  Reason         : %s\r\n", status.reason);
    commandPrintf(out, "===============================\r\n");
}

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
    commandPrintf(out, "  tasks    - แสดงสถานะ Task (heartbeat + stack)\r\n");
    commandPrintf(out, "  crash    - แสดง last crash/task stage ล่าสุด\r\n");
    commandPrintf(out, "  wifi     - แสดงข้อมูล WiFi\r\n");
    commandPrintf(out, "  mqtt     - แสดงสถานะ NETPIE\r\n");
    commandPrintf(out, "  ph       - อ่านค่า pH ปัจจุบัน\r\n");
    commandPrintf(out, "  cal686   - Calibrate pH 6.86\r\n");
    commandPrintf(out, "  cal401   - Calibrate pH 4.01\r\n");
    commandPrintf(out, "  cal918   - Calibrate pH 9.18\r\n");
    commandPrintf(out, "  cal7     - Alias ของ cal686\r\n");
    commandPrintf(out, "  cal4     - Alias ของ cal401\r\n");
    commandPrintf(out, "  light    - สถานะ light controller\r\n");
    commandPrintf(out, "  light on - เปิดไฟปลูกพืช\r\n");
    commandPrintf(out, "  light off- ปิดไฟปลูกพืช\r\n");
    commandPrintf(out, "  light auto - กลับไปใช้ schedule\r\n");
    commandPrintf(out, "  light netpie - ให้ NETPIE คุมไฟ\r\n");
    commandPrintf(out, "  light web - ให้เว็บ/Pi คุมไฟ\r\n");
    commandPrintf(out, "  feed     - สถานะระบบให้อาหารปลา\r\n");
    commandPrintf(out, "  feed now - สั่งให้อาหารทันที\r\n");
    commandPrintf(out, "  feed enable - เปิด schedule ให้อาหารปลา\r\n");
    commandPrintf(out, "  feed disable - ปิด schedule ให้อาหารปลา\r\n");
    commandPrintf(out, "  feed netpie - ให้ NETPIE คุม feeder\r\n");
    commandPrintf(out, "  feed web - ให้เว็บ/Pi คุม feeder\r\n");
    commandPrintf(out, "  fan      - สถานะพัดลมระบายอากาศ\r\n");
    commandPrintf(out, "  fan on   - เปิดพัดลมแบบ manual\r\n");
    commandPrintf(out, "  fan off  - ปิดพัดลมแบบ manual\r\n");
    commandPrintf(out, "  fan auto - ให้พัดลมกลับไปทำงานแบบ auto\r\n");
    commandPrintf(out, "  auto     - สถานะ Automator\r\n");
    commandPrintf(out, "  water    - สถานะระบบน้ำ\r\n");
    commandPrintf(out, "  circ on  - เปิดปั๊มน้ำวนหลัก\r\n");
    commandPrintf(out, "  circ off - ปิดปั๊มน้ำวนหลัก\r\n");
    commandPrintf(out, "  refill on- เปิดเติมน้ำแบบ manual\r\n");
    commandPrintf(out, "  refill off- ปิดเติมน้ำแบบ manual\r\n");
    commandPrintf(out, "  route auto- refill route แบบ auto\r\n");
    commandPrintf(out, "  route fish- บังคับเติมผ่านตู้ปลา\r\n");
    commandPrintf(out, "  route sump- บังคับเติมเข้าถังรวมตรง\r\n");
    commandPrintf(out, "  water clear- ล้าง alarm ระบบน้ำ\r\n");
    commandPrintf(out, "  pump a   - ทดสอบปั๊ม A (~%.1f วินาที / %.1f mL)\r\n", PUMP_TEST_DURATION_MS / 1000.0f, HW_TEST_PUMP_TEST_VOLUME_ML);
    commandPrintf(out, "  pump b   - ทดสอบปั๊ม B (~%.1f วินาที / %.1f mL)\r\n", PUMP_TEST_DURATION_MS / 1000.0f, HW_TEST_PUMP_TEST_VOLUME_ML);
    commandPrintf(out, "  pump stop- หยุดปั๊มทั้งหมด\r\n");
    commandPrintf(out, "  version  - แสดง Firmware Version\r\n");
    commandPrintf(out, "  reboot   - รีสตาร์ทบอร์ด\r\n");
    commandPrintf(out, "  reset    - Factory Reset (ล้างค่า Calibration)\r\n");
    commandPrintf(out, "=========================================\r\n");
}

/**
 * @brief แสดงค่าเซ็นเซอร์ทั้งหมด
 */
static void _showStatus(CommandOutput_t out) {
    float lastTds = tdsGetLastValue();
    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== SENSOR STATUS ==========\r\n");
    commandPrintf(out, "  Water Temp : %.2f C\r\n", tempRead());
    commandPrintf(out, "  Air Temp   : %.2f C\r\n", dhtReadTemperature());
    commandPrintf(out, "  Humidity   : %.2f %%\r\n", dhtReadHumidity());
    commandPrintf(out, "  TDS        : %.0f ppm\r\n", lastTds);
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
    char crashInfo[160];
    if (systemGetLastCrashInfo(crashInfo, sizeof(crashInfo))) {
        commandPrintf(out, "  Last Crash     : %s\r\n", crashInfo);
    } else {
        commandPrintf(out, "  Last Crash     : None\r\n");
    }
    commandPrintf(out, "===================================\r\n");
}

static void _showCrashInfo(CommandOutput_t out) {
    char crashInfo[160];

    commandPrintf(out, "\r\n");
    commandPrintf(out, "========== LAST CRASH ==========%s", "\r\n");
    if (systemGetLastCrashInfo(crashInfo, sizeof(crashInfo))) {
        commandPrintf(out, "  %s\r\n", crashInfo);
    } else {
        commandPrintf(out, "  No persisted crash info\r\n");
    }
    commandPrintf(out, "===============================\r\n");
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
    int pass = 0, fail = 0, warn = 0;
    
    commandPrintf(out, "\r\n");
    commandPrintf(out, "╔═══════════════════════════════════════════╗\r\n");
    commandPrintf(out, "║     AQUAPONICS FULL SYSTEM DIAGNOSTIC     ║\r\n");
    commandPrintf(out, "║          Firmware %s                    ║\r\n", systemGetVersion());
    commandPrintf(out, "╚═══════════════════════════════════════════╝\r\n\r\n");
    
    // ─── 1. CONNECTIVITY ──────────────────────────────────────
    commandPrintf(out, "─── 1. CONNECTIVITY ───────────────────────\r\n");
    
    bool wifiOk = wifiIsConnected();
    commandPrintf(out, "  WiFi         : %s\r\n", wifiOk ? "✅ CONNECTED" : "❌ DISCONNECTED");
    wifiOk ? pass++ : fail++;
    
    if (wifiOk) {
        int rssi = WiFi.RSSI();
        const char* quality = (rssi > -50) ? "Excellent" : (rssi > -60) ? "Good" : (rssi > -70) ? "Fair" : "Weak";
        commandPrintf(out, "    SSID       : %s\r\n", WiFi.SSID().c_str());
        commandPrintf(out, "    IP         : %s\r\n", WiFi.localIP().toString().c_str());
        commandPrintf(out, "    RSSI       : %d dBm (%s)\r\n", rssi, quality);
        if (rssi < -75) { warn++; commandPrintf(out, "    ⚠️  Signal weak! Consider moving closer to router\r\n"); }
    }
    
    bool netpieOk = netpieIsConnected();
    commandPrintf(out, "  NETPIE Cloud : %s\r\n", netpieOk ? "✅ CONNECTED" : "❌ DISCONNECTED");
    netpieOk ? pass++ : fail++;
    
    bool localOk = localMqttIsConnected();
    commandPrintf(out, "  Local MQTT   : %s\r\n", localOk ? "✅ CONNECTED" : "❌ DISCONNECTED");
    localOk ? pass++ : fail++;
    
    // ─── 2. SENSORS ──────────────────────────────────────────
    commandPrintf(out, "\r\n─── 2. SENSORS ────────────────────────────\r\n");
    
    float t_water = tempRead();
    float t_air = dhtReadTemperature();
    float humid = dhtReadHumidity();
    float tds = tdsGetLastValue();
    float light = lightRead();
    float ph = phRead();
    
    // Water Temp
    bool snsEnabled = systemGetSensorEnabled(SENSOR_WATER_TEMP);
    if (!snsEnabled) {
        commandPrintf(out, "  Water Temp   : ⏸️  DISABLED\r\n");
    } else {
        bool ok = !isnan(t_water) && t_water > 0;
        commandPrintf(out, "  Water Temp   : %s %.1f °C\r\n", ok ? "✅" : "❌", t_water);
        ok ? pass++ : fail++;
    }
    
    // Air Temp
    snsEnabled = systemGetSensorEnabled(SENSOR_AIR_TEMP);
    if (!snsEnabled) {
        commandPrintf(out, "  Air Temp     : ⏸️  DISABLED\r\n");
        commandPrintf(out, "  Humidity     : ⏸️  DISABLED\r\n");
    } else {
        bool ok1 = !isnan(t_air);
        bool ok2 = !isnan(humid);
        commandPrintf(out, "  Air Temp     : %s %.1f °C\r\n", ok1 ? "✅" : "❌", t_air);
        commandPrintf(out, "  Humidity     : %s %.1f %%\r\n", ok2 ? "✅" : "❌", humid);
        ok1 ? pass++ : fail++;
        ok2 ? pass++ : fail++;
    }
    
    // TDS
    snsEnabled = systemGetSensorEnabled(SENSOR_TDS);
    if (!snsEnabled) {
        commandPrintf(out, "  TDS          : ⏸️  DISABLED\r\n");
    } else {
        bool ok = (tds >= 0);
        commandPrintf(out, "  TDS          : %s %.0f ppm\r\n", ok ? "✅" : "❌", tds);
        ok ? pass++ : fail++;
    }
    
    // pH
    snsEnabled = systemGetSensorEnabled(SENSOR_PH);
    if (!snsEnabled) {
        commandPrintf(out, "  pH           : ⏸️  DISABLED\r\n");
    } else {
        bool ok = (ph >= 0 && ph <= 14);
        commandPrintf(out, "  pH           : %s %.2f\r\n", ok ? "✅" : "❌", ph);
        ok ? pass++ : fail++;
    }
    
    // Light
    snsEnabled = systemGetSensorEnabled(SENSOR_LIGHT);
    if (!snsEnabled) {
        commandPrintf(out, "  Light        : ⏸️  DISABLED\r\n");
    } else {
        bool ok = (light >= 0);
        commandPrintf(out, "  Light        : %s %.0f lux\r\n", ok ? "✅" : "❌", light);
        ok ? pass++ : fail++;
    }
    
    // ─── 3. I2C BUS SCAN ────────────────────────────────────
    commandPrintf(out, "\r\n─── 3. I2C BUS SCAN ───────────────────────\r\n");
    {
        int i2cDevices = 0;
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                const char* devName = "Unknown";
                if (addr == 0x23 || addr == 0x5C) devName = "BH1750 (Light)";
                else if (addr >= 0x48 && addr <= 0x4F) devName = "ADS1115/PCF8591";
                else if (addr == 0x27 || addr == 0x3F) devName = "LCD (I2C)";
                else if (addr == 0x3C || addr == 0x3D) devName = "OLED (SSD1306)";
                commandPrintf(out, "  0x%02X : %s ✅\r\n", addr, devName);
                i2cDevices++;
            }
        }
        if (i2cDevices == 0) {
            commandPrintf(out, "  (No I2C devices found)\r\n");
            if (systemGetSensorEnabled(SENSOR_LIGHT)) { fail++; }
        } else {
            commandPrintf(out, "  Total: %d device(s) found\r\n", i2cDevices);
            pass++;
        }
    }
    
    // ─── 4. PUMP/RELAY GPIO ─────────────────────────────────
    commandPrintf(out, "\r\n─── 4. PUMP RELAY GPIO ────────────────────\r\n");
    commandPrintf(out, "  Relay logic  : Active LOW\r\n");
    {
        // Read current GPIO states (PUMP_OFF = HIGH for active low)
        int pinA = digitalRead(PUMP_NUTRIENT_A_PIN);
        int pinB = digitalRead(PUMP_NUTRIENT_B_PIN);
        
        commandPrintf(out, "  Pump A (GPIO %2d) : %s\r\n", PUMP_NUTRIENT_A_PIN, 
                      (pinA == PUMP_OFF) ? "✅ OFF (idle)" : "⚠️  ON!");
        commandPrintf(out, "  Pump B (GPIO %2d) : %s\r\n", PUMP_NUTRIENT_B_PIN, 
                      (pinB == PUMP_OFF) ? "✅ OFF (idle)" : "⚠️  ON!");
        
        bool allIdle = (pinA == PUMP_OFF && pinB == PUMP_OFF);

        if (allIdle) {
            pass++;
        } else {
            warn++;
            commandPrintf(out, "  ⚠️  One or more pumps are active!\r\n");
        }
        commandPrintf(out, "  Use 'pump a', 'pump b', 'pump stop' to test or stop dosing pumps\r\n");
    }
    
    // ─── 5. AUTOMATOR ───────────────────────────────────────
    commandPrintf(out, "\r\n─── 5. AUTOMATION ENGINE ──────────────────\r\n");
    {
        AutomatorConfig cfg;
        automatorGetConfig(&cfg);
        AutomatorState state = automatorGetCurrentState();
        
        commandPrintf(out, "  Enabled      : %s\r\n", cfg.enabled ? "✅ YES" : "⏸️  NO");
        commandPrintf(out, "  State        : %s\r\n", automatorGetStateString(state));
        commandPrintf(out, "  Reason       : %s\r\n", automatorGetActionReason());
        commandPrintf(out, "  Target TDS   : %.0f ppm\r\n", cfg.targetTds);
        commandPrintf(out, "  Target pH    : %.1f\r\n", cfg.targetPh);
        commandPrintf(out, "  Time Left    : %d s\r\n", automatorGetTimeRemainingSec());
        pass++; // Automator initialized = pass
    }
    
    // ─── 6. TASK HEALTH ─────────────────────────────────────
    commandPrintf(out, "\r\n─── 6. FREERTOS TASK HEALTH ───────────────\r\n");
    {
        bool allAlive = true;
        for (int i = 0; i < TASK_ID_COUNT; i++) {
            unsigned long age = systemGetTaskHeartbeatAge((TaskId_t)i);
            bool stuck = (age > TASK_STUCK_THRESHOLD_MS);
            const char* status = (age == 0) ? "NOT STARTED" : stuck ? "❌ STUCK!" : "✅ OK";
            commandPrintf(out, "  %-12s : %s (%lu ms ago)\r\n", TASK_NAMES[i], status, age);
            if (stuck || age == 0) allAlive = false;
        }
        allAlive ? pass++ : fail++;
        
        // Stack watermarks
        commandPrintf(out, "  --- Stack Watermarks ---\r\n");
        systemPrintStackInfo();
    }
    
    // ─── 7. SYSTEM RESOURCES ────────────────────────────────
    commandPrintf(out, "\r\n─── 7. SYSTEM RESOURCES ───────────────────\r\n");
    {
        SystemHealth_t health;
        systemGetHealth(&health);
        
        commandPrintf(out, "  Uptime       : %lu s\r\n", health.uptimeMs / 1000);
        commandPrintf(out, "  Free Heap    : %lu / %lu B\r\n", health.freeHeap, health.heapSize);
        commandPrintf(out, "  Min Heap     : %lu B\r\n", health.minFreeHeap);
        
        if (health.minFreeHeap < 20000) {
            commandPrintf(out, "  ⚠️  Low memory! Min heap below 20KB\r\n");
            warn++;
        } else {
            pass++;
        }
        
        commandPrintf(out, "  CPU Temp     : %.1f °C\r\n", health.cpuTemp);
        if (health.cpuTemp > 70.0f) {
            commandPrintf(out, "  ⚠️  CPU temperature high!\r\n");
            warn++;
        }
        
        commandPrintf(out, "  WDT Resets   : %u %s\r\n", health.watchdogResets, health.watchdogResets > 0 ? "⚠️" : "✅");
        commandPrintf(out, "  Reconnects   : WiFi=%u, MQTT=%u\r\n", health.wifiReconnects, health.mqttReconnects);
        commandPrintf(out, "  Reset Reason : %s\r\n", health.resetReason);
        char crashInfo[160];
        if (systemGetLastCrashInfo(crashInfo, sizeof(crashInfo))) {
            commandPrintf(out, "  Last Crash   : %s\r\n", crashInfo);
        } else {
            commandPrintf(out, "  Last Crash   : None\r\n");
        }
    }
    
    // ─── 8. NTP TIME SYNC ───────────────────────────────────
    commandPrintf(out, "\r\n─── 8. NTP TIME SYNC ──────────────────────\r\n");
    {
        struct tm timeinfo;
        bool timeOk = getLocalTime(&timeinfo, 100);
        if (timeOk) {
            char timeStr[30];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            commandPrintf(out, "  Local Time   : %s ✅\r\n", timeStr);
            commandPrintf(out, "  Timezone     : GMT+7 (ICT)\r\n");
            pass++;
        } else {
            commandPrintf(out, "  Local Time   : ❌ UNSYNCED\r\n");
            fail++;
        }
    }
    
    // ─── SUMMARY ────────────────────────────────────────────
    int total = pass + fail;
    int score = (total > 0) ? (pass * 100 / total) : 0;
    
    commandPrintf(out, "\r\n╔═══════════════════════════════════════════╗\r\n");
    commandPrintf(out, "║  RESULT: %d/%d PASSED (%d%%)  ", pass, total, score);
    if (warn > 0) commandPrintf(out, "⚠️ %d WARN  ", warn);
    commandPrintf(out, "\r\n");
    commandPrintf(out, "║  %s\r\n", score >= 80 ? "🟢 SYSTEM HEALTHY" : score >= 50 ? "🟡 NEEDS ATTENTION" : "🔴 CRITICAL ISSUES");
    commandPrintf(out, "╚═══════════════════════════════════════════╝\r\n");
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
    else if (strcmp(cleanCmd, "crash") == 0) {
        _showCrashInfo(output);
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
    else if (strcmp(cleanCmd, "cal686") == 0 || strcmp(cleanCmd, "cal7") == 0) {
        commandPrintf(output, "[PH] Calibrating pH 6.86...\r\n");
        phCalibratePh686();
        commandPrintf(output, "[PH] Calibration complete!\r\n");
    }
    else if (strcmp(cleanCmd, "cal401") == 0 || strcmp(cleanCmd, "cal4") == 0) {
        commandPrintf(output, "[PH] Calibrating pH 4.01...\r\n");
        phCalibratePh401();
        commandPrintf(output, "[PH] Calibration complete!\r\n");
    }
    else if (strcmp(cleanCmd, "cal918") == 0) {
        commandPrintf(output, "[PH] Calibrating pH 9.18...\r\n");
        phCalibratePh918();
        commandPrintf(output, "[PH] Calibration complete!\r\n");
    }
    else if (strcmp(cleanCmd, "light") == 0) {
        _showLightController(output);
    }
    else if (strcmp(cleanCmd, "light on") == 0) {
        lightCtrlSetEnabled(0);
        lightCtrlSetManualState(true);
        lightCtrlSetState(true);
        commandPrintf(output, "[LIGHT] Forced ON\r\n");
    }
    else if (strcmp(cleanCmd, "light off") == 0) {
        lightCtrlSetEnabled(0);
        lightCtrlSetManualState(false);
        lightCtrlSetState(false);
        commandPrintf(output, "[LIGHT] Forced OFF\r\n");
    }
    else if (strcmp(cleanCmd, "light auto") == 0) {
        lightCtrlSetEnabled(1);  // Re-enable schedule
        commandPrintf(output, "[LIGHT] Auto mode (schedule enabled)\r\n");
    }
    else if (strcmp(cleanCmd, "light netpie") == 0) {
        lightCtrlSetCommandSource(COMMAND_SOURCE_NETPIE);
        if (!netpieRequestShadowSync()) {
            commandPrintf(output, "[LIGHT] Warning: NETPIE shadow refresh request failed\r\n");
        }
        commandPrintf(output, "[LIGHT] Control source -> NETPIE\r\n");
    }
    else if (strcmp(cleanCmd, "light web") == 0) {
        lightCtrlSetCommandSource(COMMAND_SOURCE_LOCAL_WEB);
        commandPrintf(output, "[LIGHT] Control source -> LOCAL WEB\r\n");
    }
    else if (strcmp(cleanCmd, "feed") == 0) {
        _showFishFeeder(output);
    }
    else if (strcmp(cleanCmd, "feed now") == 0) {
        if (fishFeederStartManualFeed("Manual feed triggered from CLI")) {
            commandPrintf(output, "[FEED] Manual feed started\r\n");
        } else {
            commandPrintf(output, "[FEED] Manual feed rejected\r\n");
        }
    }
    else if (strcmp(cleanCmd, "feed enable") == 0) {
        fishFeederSetEnabled(true);
        commandPrintf(output, "[FEED] Schedule enabled\r\n");
    }
    else if (strcmp(cleanCmd, "feed disable") == 0) {
        fishFeederSetEnabled(false);
        commandPrintf(output, "[FEED] Schedule disabled\r\n");
    }
    else if (strcmp(cleanCmd, "feed netpie") == 0) {
        fishFeederSetCommandSource(COMMAND_SOURCE_NETPIE);
        commandPrintf(output, "[FEED] Control source -> NETPIE\r\n");
    }
    else if (strcmp(cleanCmd, "feed web") == 0) {
        fishFeederSetCommandSource(COMMAND_SOURCE_LOCAL_WEB);
        commandPrintf(output, "[FEED] Control source -> LOCAL WEB\r\n");
    }
    else if (strcmp(cleanCmd, "fan") == 0) {
        _showFanController(output);
    }
    else if (strcmp(cleanCmd, "fan on") == 0) {
        fanCtrlSetEnabled(true);
        fanCtrlSetManualState(true);
        commandPrintf(output, "[FAN] Manual ON\r\n");
    }
    else if (strcmp(cleanCmd, "fan off") == 0) {
        fanCtrlSetEnabled(true);
        fanCtrlSetManualState(false);
        commandPrintf(output, "[FAN] Manual OFF\r\n");
    }
    else if (strcmp(cleanCmd, "fan auto") == 0) {
        fanCtrlSetEnabled(true);
        fanCtrlSetAutoMode(true);
        commandPrintf(output, "[FAN] AUTO mode restored\r\n");
    }
    else if (strcmp(cleanCmd, "water") == 0) {
        _showWaterSystem(output);
    }
    else if (strcmp(cleanCmd, "circ on") == 0) {
        waterSystemSetCirculationEnabled(true);
        commandPrintf(output, "[WATER] Circulation enabled\r\n");
    }
    else if (strcmp(cleanCmd, "circ off") == 0) {
        waterSystemSetCirculationEnabled(false);
        commandPrintf(output, "[WATER] Circulation disabled\r\n");
    }
    else if (strcmp(cleanCmd, "refill on") == 0) {
        waterSystemSetManualRefill(true);
        commandPrintf(output, "[WATER] Manual refill requested\r\n");
    }
    else if (strcmp(cleanCmd, "refill off") == 0) {
        waterSystemSetManualRefill(false);
        commandPrintf(output, "[WATER] Manual refill stopped\r\n");
    }
    else if (strcmp(cleanCmd, "route auto") == 0) {
        waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_AUTO);
        commandPrintf(output, "[WATER] Preferred refill route set to AUTO\r\n");
    }
    else if (strcmp(cleanCmd, "route fish") == 0) {
        waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_FISH_TANK);
        commandPrintf(output, "[WATER] Preferred refill route set to FISH_TANK\r\n");
    }
    else if (strcmp(cleanCmd, "route sump") == 0) {
        waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_SUMP_DIRECT);
        commandPrintf(output, "[WATER] Preferred refill route set to SUMP_DIRECT\r\n");
    }
    else if (strcmp(cleanCmd, "water clear") == 0) {
        waterSystemClearAlarm();
        commandPrintf(output, "[WATER] Alarm cleared\r\n");
    }
    // === Automator Status ===
    else if (strcmp(cleanCmd, "auto") == 0) {
        AutomatorConfig cfg;
        automatorGetConfig(&cfg);
        commandPrintf(output, "\r\n========== AUTOMATOR ==========\r\n");
        commandPrintf(output, "  Enabled    : %s\r\n", cfg.enabled ? "YES" : "NO");
        commandPrintf(output, "  State      : %s\r\n", automatorGetStateString(automatorGetCurrentState()));
        commandPrintf(output, "  Reason     : %s\r\n", automatorGetActionReason());
        commandPrintf(output, "  Target TDS : %.1f ppm\r\n", cfg.targetTds);
        commandPrintf(output, "  Target pH  : %.1f\r\n", cfg.targetPh);
        commandPrintf(output, "  Time Left  : %d s\r\n", automatorGetTimeRemainingSec());
        commandPrintf(output, "===============================\r\n");
    }
    // === Pump Test Commands ===
    else if (strcmp(cleanCmd, "pump a") == 0) {
        _pumpTestPin = PUMP_NUTRIENT_A_PIN;
        _pumpTestStartMs = millis();
        _pumpTestActive = true;
        digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_ON);
        commandPrintf(output, "[PUMP] Testing Nutrient A (GPIO %d) for %.1f seconds (~%.1f mL)...\r\n",
                  PUMP_NUTRIENT_A_PIN,
                  PUMP_TEST_DURATION_MS / 1000.0f,
                  HW_TEST_PUMP_TEST_VOLUME_ML);
        LOG_INFO("[PUMP TEST] Nutrient A ON (GPIO %d)", PUMP_NUTRIENT_A_PIN);
    }
    else if (strcmp(cleanCmd, "pump b") == 0) {
        _pumpTestPin = PUMP_NUTRIENT_B_PIN;
        _pumpTestStartMs = millis();
        _pumpTestActive = true;
        digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_ON);
        commandPrintf(output, "[PUMP] Testing Nutrient B (GPIO %d) for %.1f seconds (~%.1f mL)...\r\n",
                  PUMP_NUTRIENT_B_PIN,
                  PUMP_TEST_DURATION_MS / 1000.0f,
                  HW_TEST_PUMP_TEST_VOLUME_ML);
        LOG_INFO("[PUMP TEST] Nutrient B ON (GPIO %d)", PUMP_NUTRIENT_B_PIN);
    }
    else if (strcmp(cleanCmd, "pump stop") == 0) {
        digitalWrite(PUMP_NUTRIENT_A_PIN, PUMP_OFF);
        digitalWrite(PUMP_NUTRIENT_B_PIN, PUMP_OFF);
        _pumpTestActive = false;
        commandPrintf(output, "[PUMP] All pumps STOPPED\r\n");
        LOG_INFO("[PUMP TEST] All pumps OFF (manual stop)");
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
    else if (strcmp(cleanCmd, "tasks") == 0) {
        commandPrintf(output, "\r\n========== TASK HEALTH ==========\r\n");
        for (int i = 0; i < TASK_ID_COUNT; i++) {
            unsigned long age = systemGetTaskHeartbeatAge((TaskId_t)i);
            const char* status = (age == 0) ? "NOT STARTED" : (age > TASK_STUCK_THRESHOLD_MS) ? "STUCK!" : "OK";
            commandPrintf(output, "  %-12s : %s (heartbeat %lums ago)\r\n", TASK_NAMES[i], status, age);
        }
        systemPrintStackInfo();
        commandPrintf(output, "=================================\r\n");
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

void commandPumpTestTick(void) {
    if (_pumpTestActive && (millis() - _pumpTestStartMs >= PUMP_TEST_DURATION_MS)) {
        digitalWrite(_pumpTestPin, PUMP_OFF);
        _pumpTestActive = false;
        LOG_INFO("[PUMP TEST] Auto-off (GPIO %d) after %d ms", _pumpTestPin, PUMP_TEST_DURATION_MS);
    }
}
