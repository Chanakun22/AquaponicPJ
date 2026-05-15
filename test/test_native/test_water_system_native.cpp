#include <unity.h>

#include "mock/Arduino.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <string>

unsigned long _mockMillis = 0;
static time_t _mockUnixEpochBase = 1700000000;

static const int MOCK_PIN_CAPACITY = 64;
static int _mockPinStates[MOCK_PIN_CAPACITY] = {0};
static int _mockPinModes[MOCK_PIN_CAPACITY] = {0};

unsigned long millis() { return _mockMillis; }
unsigned long micros() { return _mockMillis * 1000UL; }
void delay(unsigned long ms) { _mockMillis += ms; }
void delayMicroseconds(unsigned int) {}

extern "C" time_t time(time_t* value) {
    time_t now = _mockUnixEpochBase + static_cast<time_t>(_mockMillis / 1000UL);
    if (value) {
        *value = now;
    }
    return now;
}

int analogRead(uint8_t) { return 0; }
void analogSetAttenuation(int) {}

void pinMode(uint8_t pin, uint8_t mode) {
    if (pin < MOCK_PIN_CAPACITY) {
        _mockPinModes[pin] = mode;
    }
}

void digitalWrite(uint8_t pin, uint8_t value) {
    if (pin < MOCK_PIN_CAPACITY) {
        _mockPinStates[pin] = value;
    }
}

int digitalRead(uint8_t pin) {
    if (pin < MOCK_PIN_CAPACITY) {
        return _mockPinStates[pin];
    }
    return HIGH;
}

struct MockSerial {
    void begin(unsigned long) {}
    void print(const char*) {}
    void print(char) {}
    void print(int) {}
    void print(float, int = 2) {}
    void println(const char*) {}
    void println(char) {}
    void println(int) {}
    void println(float, int = 2) {}
    void println() {}
    void printf(const char*, ...) {}
};

MockSerial Serial;

static std::map<std::string, std::map<std::string, unsigned long>> _ulongPrefs;
static std::map<std::string, std::map<std::string, bool>> _boolPrefs;
static std::map<std::string, std::map<std::string, uint8_t>> _ucharPrefs;

class Preferences {
public:
    bool begin(const char* name, bool) {
        _namespace = name ? name : "";
        return true;
    }

    void end() {}

    bool getBool(const char* key, bool def = false) {
        return lookup(_boolPrefs, key, def);
    }

    unsigned long getULong(const char* key, unsigned long def = 0) {
        return lookup(_ulongPrefs, key, def);
    }

    uint8_t getUChar(const char* key, uint8_t def = 0) {
        return lookup(_ucharPrefs, key, def);
    }

    size_t putBool(const char* key, bool value) {
        _boolPrefs[_namespace][key] = value;
        return sizeof(bool);
    }

    size_t putULong(const char* key, unsigned long value) {
        _ulongPrefs[_namespace][key] = value;
        return sizeof(unsigned long);
    }

    size_t putUChar(const char* key, uint8_t value) {
        _ucharPrefs[_namespace][key] = value;
        return sizeof(uint8_t);
    }

    static void resetAll() {
        _ulongPrefs.clear();
        _boolPrefs.clear();
        _ucharPrefs.clear();
    }

private:
    template <typename TMap, typename TValue>
    TValue lookup(const TMap& store, const char* key, TValue def) const {
        typename TMap::const_iterator nsIt = store.find(_namespace);
        if (nsIt == store.end()) {
            return def;
        }

        typename TMap::mapped_type::const_iterator valueIt = nsIt->second.find(key ? key : "");
        if (valueIt == nsIt->second.end()) {
            return def;
        }

        return valueIt->second;
    }

    std::string _namespace;
};

#define WATER_SYSTEM_TEST_OVERRIDES 1
#include "../../src/waterSystem.cpp"

static void reset_inputs(void) {
    for (int pin = 0; pin < MOCK_PIN_CAPACITY; ++pin) {
        _mockPinStates[pin] = HIGH;
        _mockPinModes[pin] = 0;
    }
}

static void set_level_state(bool levelLow, bool levelHigh, bool overflowActive = false) {
    _mockPinStates[SUMP_LEVEL_LOW_PIN] = levelLow ? WATER_LEVEL_TRIGGER_STATE : HIGH;
    _mockPinStates[SUMP_LEVEL_HIGH_PIN] = levelHigh ? WATER_LEVEL_TRIGGER_STATE : HIGH;
    _mockPinStates[FISH_TANK_OVERFLOW_PIN] = overflowActive ? OVERFLOW_SENSOR_TRIGGER_STATE : HIGH;
}

static WaterSystemStatus current_status(void) {
    WaterSystemStatus status;
    waterSystemGetStatus(&status);
    return status;
}

static WaterSystemConfig current_config(void) {
    WaterSystemConfig config;
    waterSystemGetConfig(&config);
    return config;
}

static void setup_with_config(bool allowDirectSumpRefill,
                              unsigned long fishRefillIntervalMs,
                              unsigned long fishRefillMaxRuntimeMs) {
    Preferences::resetAll();
    _mockMillis = 0;
    _mockUnixEpochBase = 1700000000;
    reset_inputs();
    waterSystemTestResetOverrides();
    waterSystemSetup();
    waterSystemSetConfig(
        true,
        true,
        120000UL,
        0UL,
        WATER_REFILL_ROUTE_AUTO,
        allowDirectSumpRefill,
        fishRefillIntervalMs,
        fishRefillMaxRuntimeMs
    );
}

static void setup_disabled_water_system(void) {
    Preferences::resetAll();
    _mockMillis = 0;
    _mockUnixEpochBase = 1700000000;
    reset_inputs();
    waterSystemTestResetOverrides();
    waterSystemSetup();
    waterSystemSetConfig(
        false,
        false,
        120000UL,
        0UL,
        WATER_REFILL_ROUTE_AUTO,
        false,
        60000UL,
        5000UL
    );
}

static void reboot_with_saved_prefs(unsigned long downtimeSec) {
    _mockUnixEpochBase = time(NULL) + static_cast<time_t>(downtimeSec);
    _mockMillis = 0;
    reset_inputs();
    waterSystemTestResetOverrides();
    waterSystemSetup();
}

void test_water_auto_prefers_fish_route_first() {
    setup_with_config(true, 60000UL, 5000UL);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_TRUE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_FISH_TANK, status.activeRoute);
    TEST_ASSERT_TRUE(status.fishTankRefillOutput);
    TEST_ASSERT_FALSE(status.mixTankRefillOutput);
}

void test_water_auto_falls_back_to_sump_after_fish_limit() {
    setup_with_config(true, 60000UL, 5000UL);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    _mockMillis = 7000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_TRUE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_SUMP_DIRECT, status.activeRoute);
    TEST_ASSERT_FALSE(status.fishTankRefillOutput);
    TEST_ASSERT_TRUE(status.mixTankRefillOutput);
}

void test_water_marks_fish_cooldown_after_fish_refill_stops() {
    setup_with_config(false, 60000UL, 5000UL);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    _mockMillis = 7000;
    waterSystemLoop();

    _mockMillis = 8000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_FALSE(status.refillOutput);
    TEST_ASSERT_FALSE(status.fishRefillReady);
    TEST_ASSERT_TRUE(status.fishRefillWaitRemainingMs > 0);
    TEST_ASSERT_EQUAL(WATER_STATE_WAIT_REFILL_INTERVAL, status.state);
}

void test_water_does_not_alarm_on_conflicting_levels_when_refill_is_disabled() {
    setup_disabled_water_system();
    set_level_state(true, true, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_FALSE(status.alarmActive);
    TEST_ASSERT_EQUAL(WATER_STATE_IDLE, status.state);
}

void test_water_does_not_latch_alarm_when_both_levels_are_active() {
    setup_with_config(true, 60000UL, 5000UL);
    set_level_state(true, true, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_FALSE(status.alarmActive);
    TEST_ASSERT_FALSE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_STATE_IDLE, status.state);
}

void test_water_fish_route_drives_valve_to_fish_state() {
    setup_with_config(false, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_FISH_TANK);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    TEST_ASSERT_EQUAL(REFILL_ROUTE_TO_FISH_STATE, _mockPinStates[REFILL_ROUTE_VALVE_PIN]);
}

void test_water_sump_route_drives_valve_to_sump_state() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_SUMP_DIRECT);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    TEST_ASSERT_EQUAL(REFILL_ROUTE_TO_SUMP_STATE, _mockPinStates[REFILL_ROUTE_VALVE_PIN]);
}

void test_water_manual_fish_route_ignores_mix_level_sensors() {
    setup_with_config(false, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_FISH_TANK);
    waterSystemSetManualRefill(true);
    set_level_state(false, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_TRUE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_FISH_TANK, status.activeRoute);
    TEST_ASSERT_TRUE(status.fishTankRefillOutput);
    TEST_ASSERT_FALSE(status.mixTankRefillOutput);
}

void test_water_auto_fish_route_keeps_running_until_limit_after_low_clears() {
    setup_with_config(false, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_FISH_TANK);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    set_level_state(false, false, false);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_TRUE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_FISH_TANK, status.activeRoute);
    TEST_ASSERT_TRUE(status.fishTankRefillOutput);
}

void test_water_manual_fish_route_stops_on_overflow_sensor() {
    setup_with_config(false, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_FISH_TANK);
    waterSystemSetManualRefill(true);
    set_level_state(false, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    set_level_state(false, false, true);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();
    WaterSystemConfig cfg = current_config();

    TEST_ASSERT_FALSE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_NONE, status.activeRoute);
    TEST_ASSERT_FALSE(status.fishRefillReady);
    TEST_ASSERT_FALSE(cfg.manualRefill);
}

void test_water_manual_fish_route_stops_on_mix_tank_high_level() {
    setup_with_config(false, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_FISH_TANK);
    waterSystemSetManualRefill(true);
    set_level_state(false, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    set_level_state(false, true, false);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();
    WaterSystemConfig cfg = current_config();

    TEST_ASSERT_FALSE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_NONE, status.activeRoute);
    TEST_ASSERT_FALSE(status.fishTankRefillOutput);
    TEST_ASSERT_FALSE(cfg.manualRefill);
}

void test_water_manual_fish_route_clears_manual_mode_after_runtime_limit() {
    setup_with_config(false, 60000UL, 2000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_FISH_TANK);
    waterSystemSetManualRefill(true);
    set_level_state(false, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    _mockMillis = 4000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();
    WaterSystemConfig cfg = current_config();

    TEST_ASSERT_FALSE(status.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_NONE, status.activeRoute);
    TEST_ASSERT_FALSE(status.fishTankRefillOutput);
    TEST_ASSERT_FALSE(cfg.manualRefill);
}

void test_water_stop_after_sump_refill_closes_route_actuator() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_SUMP_DIRECT);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    TEST_ASSERT_EQUAL(REFILL_ROUTE_TO_SUMP_STATE, _mockPinStates[REFILL_ROUTE_VALVE_PIN]);

    waterSystemSetConfig(
        false,
        false,
        120000UL,
        0UL,
        WATER_REFILL_ROUTE_AUTO,
        false,
        60000UL,
        5000UL
    );
    waterSystemSetManualRefill(false);

    _mockMillis = 2000;
    waterSystemLoop();

    TEST_ASSERT_EQUAL(REFILL_ROUTE_TO_FISH_STATE, _mockPinStates[REFILL_ROUTE_VALVE_PIN]);
}

void test_water_sump_refill_stops_when_both_level_sensors_are_active() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_SUMP_DIRECT);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus runningStatus = current_status();

    TEST_ASSERT_TRUE(runningStatus.refillOutput);
    TEST_ASSERT_TRUE(runningStatus.mixTankRefillOutput);
    TEST_ASSERT_EQUAL(REFILL_ROUTE_TO_SUMP_STATE, _mockPinStates[REFILL_ROUTE_VALVE_PIN]);

    set_level_state(true, true, false);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus stoppedStatus = current_status();

    TEST_ASSERT_FALSE(stoppedStatus.refillOutput);
    TEST_ASSERT_FALSE(stoppedStatus.mixTankRefillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_NONE, stoppedStatus.activeRoute);
    TEST_ASSERT_EQUAL(REFILL_ROUTE_TO_FISH_STATE, _mockPinStates[REFILL_ROUTE_VALVE_PIN]);
}

void test_water_manual_sump_route_stops_on_mix_tank_high_level() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_SUMP_DIRECT);
    waterSystemSetManualRefill(true);
    set_level_state(false, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus runningStatus = current_status();

    TEST_ASSERT_TRUE(runningStatus.refillOutput);
    TEST_ASSERT_TRUE(runningStatus.mixTankRefillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_SUMP_DIRECT, runningStatus.activeRoute);

    set_level_state(false, true, false);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus stoppedStatus = current_status();
    WaterSystemConfig cfg;
    waterSystemGetConfig(&cfg);

    TEST_ASSERT_FALSE(stoppedStatus.refillOutput);
    TEST_ASSERT_FALSE(stoppedStatus.mixTankRefillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_NONE, stoppedStatus.activeRoute);
    TEST_ASSERT_FALSE(stoppedStatus.alarmActive);
    TEST_ASSERT_FALSE(cfg.manualRefill);
}

void test_water_without_circulation_pump_stays_blocked() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemTestSetCirculationPumpPresent(false);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_EQUAL(WATER_STATE_BLOCKED, status.state);
    TEST_ASSERT_FALSE(status.circulationOutput);
    TEST_ASSERT_FALSE(status.refillOutput);
    TEST_ASSERT_FALSE(status.mixTankControlZone);
}

void test_water_control_zone_requires_running_circulation() {
    setup_with_config(true, 60000UL, 5000UL);
    set_level_state(false, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus idleStatus = current_status();

    TEST_ASSERT_TRUE(idleStatus.circulationOutput);
    TEST_ASSERT_TRUE(idleStatus.mixTankControlZone);

    waterSystemSetCirculationEnabled(false);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus stoppedStatus = current_status();

    TEST_ASSERT_FALSE(stoppedStatus.circulationOutput);
    TEST_ASSERT_FALSE(stoppedStatus.mixTankControlZone);
}

void test_water_control_zone_is_false_during_refill_and_settling() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_SUMP_DIRECT);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus refillStatus = current_status();

    TEST_ASSERT_TRUE(refillStatus.mixTankRefillOutput);
    TEST_ASSERT_TRUE(refillStatus.waterDilutionActive);
    TEST_ASSERT_FALSE(refillStatus.mixTankControlZone);

    set_level_state(false, true, false);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus settlingStatus = current_status();

    TEST_ASSERT_TRUE(settlingStatus.mixTankSettlingActive);
    TEST_ASSERT_FALSE(settlingStatus.mixTankControlZone);
}

void test_water_apply_config_preserves_fish_cooldown() {
    setup_with_config(false, 60000UL, 5000UL);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    _mockMillis = 7000;
    waterSystemLoop();
    WaterSystemStatus cooldownStatus = current_status();
    TEST_ASSERT_FALSE(cooldownStatus.fishRefillReady);
    TEST_ASSERT_TRUE(cooldownStatus.fishRefillWaitRemainingMs > 0);

    waterSystemSetConfig(
        true,
        true,
        120000UL,
        0UL,
        WATER_REFILL_ROUTE_AUTO,
        false,
        60000UL,
        5000UL
    );

    _mockMillis = 8000;
    waterSystemLoop();
    WaterSystemStatus afterApplyStatus = current_status();

    TEST_ASSERT_FALSE(afterApplyStatus.refillOutput);
    TEST_ASSERT_FALSE(afterApplyStatus.fishRefillReady);
    TEST_ASSERT_TRUE(afterApplyStatus.fishRefillWaitRemainingMs > 0);
    TEST_ASSERT_EQUAL(WATER_STATE_WAIT_REFILL_INTERVAL, afterApplyStatus.state);
}

void test_water_auto_does_not_fall_back_to_sump_when_levels_conflict_after_fish_stop() {
    setup_with_config(true, 60000UL, 5000UL);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus fishStatus = current_status();

    TEST_ASSERT_TRUE(fishStatus.refillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_FISH_TANK, fishStatus.activeRoute);

    set_level_state(true, true, true);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus stoppedStatus = current_status();

    TEST_ASSERT_FALSE(stoppedStatus.refillOutput);
    TEST_ASSERT_FALSE(stoppedStatus.mixTankRefillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_NONE, stoppedStatus.activeRoute);
}

void test_water_auto_fish_route_stops_on_mix_tank_high_level() {
    setup_with_config(true, 60000UL, 5000UL);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    set_level_state(true, true, false);
    _mockMillis = 2000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_FALSE(status.refillOutput);
    TEST_ASSERT_FALSE(status.mixTankRefillOutput);
    TEST_ASSERT_EQUAL(WATER_REFILL_ROUTE_NONE, status.activeRoute);
}

void test_water_persists_mix_refill_interval_across_reboot() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetConfig(
        true,
        true,
        120000UL,
        300000UL,
        WATER_REFILL_ROUTE_SUMP_DIRECT,
        false,
        60000UL,
        5000UL
    );
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    set_level_state(false, true, false);
    _mockMillis = 2000;
    waterSystemLoop();

    reboot_with_saved_prefs(180UL);
    set_level_state(true, false, false);
    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_EQUAL(WATER_STATE_WAIT_REFILL_INTERVAL, status.state);
    TEST_ASSERT_TRUE(status.fishRefillReady);
    TEST_ASSERT_TRUE(status.mixTankSettlingActive == false);
    TEST_ASSERT_TRUE(status.fishRefillWaitRemainingMs == 0);
    TEST_ASSERT_TRUE(status.reason[0] != '\0');
}

void test_water_persists_fish_wait_across_reboot() {
    setup_with_config(false, 300000UL, 5000UL);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    _mockMillis = 7000;
    waterSystemLoop();

    reboot_with_saved_prefs(180UL);
    set_level_state(true, false, false);
    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_EQUAL(WATER_STATE_WAIT_REFILL_INTERVAL, status.state);
    TEST_ASSERT_FALSE(status.fishRefillReady);
    TEST_ASSERT_TRUE(status.fishRefillWaitRemainingMs >= 118000UL);
    TEST_ASSERT_TRUE(status.fishRefillWaitRemainingMs <= 122000UL);
}

void test_water_persists_dilution_hold_across_reboot() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetPreferredRoute(WATER_REFILL_ROUTE_SUMP_DIRECT);
    set_level_state(true, false, false);

    _mockMillis = 1000;
    waterSystemLoop();

    set_level_state(false, true, false);
    _mockMillis = 2000;
    waterSystemLoop();

    reboot_with_saved_prefs(30UL);
    set_level_state(false, false, false);
    _mockMillis = 1000;
    waterSystemLoop();
    WaterSystemStatus status = current_status();

    TEST_ASSERT_EQUAL(WATER_STATE_MIX_TANK_SETTLING, status.state);
    TEST_ASSERT_TRUE(status.mixTankSettlingActive);
    TEST_ASSERT_TRUE(status.dilutionHoldRemainingMs >= 87000UL);
    TEST_ASSERT_TRUE(status.dilutionHoldRemainingMs <= 91000UL);
}

void test_water_keeps_wait_state_during_brief_low_sensor_bounce() {
    setup_with_config(true, 60000UL, 5000UL);
    waterSystemSetConfig(
        true,
        true,
        120000UL,
        300000UL,
        WATER_REFILL_ROUTE_SUMP_DIRECT,
        false,
        60000UL,
        5000UL
    );

    set_level_state(true, false, false);
    _mockMillis = 1000;
    waterSystemLoop();

    set_level_state(false, true, false);
    _mockMillis = 2000;
    waterSystemLoop();

    set_level_state(true, false, false);
    _mockMillis = 3200;
    waterSystemLoop();

    _mockMillis = 3600;
    waterSystemLoop();
    WaterSystemStatus waitingStatus = current_status();

    TEST_ASSERT_EQUAL(WATER_STATE_WAIT_REFILL_INTERVAL, waitingStatus.state);

    set_level_state(false, false, false);
    _mockMillis = 3700;
    waterSystemLoop();
    WaterSystemStatus bouncedStatus = current_status();

    TEST_ASSERT_EQUAL(WATER_STATE_WAIT_REFILL_INTERVAL, bouncedStatus.state);
    TEST_ASSERT_TRUE(bouncedStatus.fishRefillReady);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_water_auto_prefers_fish_route_first);
    RUN_TEST(test_water_auto_falls_back_to_sump_after_fish_limit);
    RUN_TEST(test_water_marks_fish_cooldown_after_fish_refill_stops);
    RUN_TEST(test_water_does_not_alarm_on_conflicting_levels_when_refill_is_disabled);
    RUN_TEST(test_water_does_not_latch_alarm_when_both_levels_are_active);
    RUN_TEST(test_water_fish_route_drives_valve_to_fish_state);
    RUN_TEST(test_water_sump_route_drives_valve_to_sump_state);
    RUN_TEST(test_water_manual_fish_route_ignores_mix_level_sensors);
    RUN_TEST(test_water_auto_fish_route_keeps_running_until_limit_after_low_clears);
    RUN_TEST(test_water_manual_fish_route_stops_on_overflow_sensor);
    RUN_TEST(test_water_manual_fish_route_stops_on_mix_tank_high_level);
    RUN_TEST(test_water_manual_fish_route_clears_manual_mode_after_runtime_limit);
    RUN_TEST(test_water_stop_after_sump_refill_closes_route_actuator);
    RUN_TEST(test_water_sump_refill_stops_when_both_level_sensors_are_active);
    RUN_TEST(test_water_manual_sump_route_stops_on_mix_tank_high_level);
    RUN_TEST(test_water_without_circulation_pump_stays_blocked);
    RUN_TEST(test_water_control_zone_requires_running_circulation);
    RUN_TEST(test_water_control_zone_is_false_during_refill_and_settling);
    RUN_TEST(test_water_apply_config_preserves_fish_cooldown);
    RUN_TEST(test_water_auto_does_not_fall_back_to_sump_when_levels_conflict_after_fish_stop);
    RUN_TEST(test_water_auto_fish_route_stops_on_mix_tank_high_level);
    RUN_TEST(test_water_persists_mix_refill_interval_across_reboot);
    RUN_TEST(test_water_persists_fish_wait_across_reboot);
    RUN_TEST(test_water_persists_dilution_hold_across_reboot);
    RUN_TEST(test_water_keeps_wait_state_during_brief_low_sensor_bounce);

    return UNITY_END();
}
