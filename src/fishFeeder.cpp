/**
 * @file fishFeeder.cpp
 * @brief Fish feeder controller implementation
 */

#include "fishFeeder.h"
#include "config.h"
#include "logger.h"
#include <Preferences.h>
#include <time.h>
#include <string.h>

static Preferences _feederPrefs;

static CommandSource _commandSource = FEEDER_DEFAULT_COMMAND_SOURCE;
static bool _enabled = FEEDER_DEFAULT_ENABLED;
static int _feedDay = FEEDER_DEFAULT_FEED_DAY;
static int _feedHour = FEEDER_DEFAULT_FEED_HOUR;
static int _feedMinute = FEEDER_DEFAULT_FEED_MINUTE;
static unsigned long _durationMs = FEEDER_DEFAULT_DURATION_MS;
static FishFeederStatus _status = {
    FEEDER_STATE_DISABLED,
    false,
    false,
    "Never",
    "Feeder not initialized"
};
static unsigned long _feedStartMs = 0;
static unsigned long _lastLoopMs = 0;
static int _lastTriggeredWeekMinute = -1;

static bool _hasOutput(void) {
#if FISH_FEEDER_PIN >= 0
    return true;
#else
    return false;
#endif
}

static void _setReason(const char* reason) {
    if (strncmp(_status.reason, reason, sizeof(_status.reason)) != 0) {
        snprintf(_status.reason, sizeof(_status.reason), "%s", reason);
        LOG_INFO("[FEEDER] %s", _status.reason);
    }
}

static void _saveConfig(void) {
    _feederPrefs.begin("fishFeed", false);
    _feederPrefs.putInt("source", (int)_commandSource);
    _feederPrefs.putBool("enabled", _enabled);
    _feederPrefs.putInt("day", _feedDay);
    _feederPrefs.putInt("hour", _feedHour);
    _feederPrefs.putInt("minute", _feedMinute);
    _feederPrefs.putULong("duration", _durationMs);
    _feederPrefs.end();
}

static void _loadConfig(void) {
    _feederPrefs.begin("fishFeed", true);
    _commandSource = (CommandSource)_feederPrefs.getInt("source", (int)FEEDER_DEFAULT_COMMAND_SOURCE);
    _enabled = _feederPrefs.getBool("enabled", FEEDER_DEFAULT_ENABLED);
    _feedDay = _feederPrefs.getInt("day", FEEDER_DEFAULT_FEED_DAY);
    _feedHour = _feederPrefs.getInt("hour", FEEDER_DEFAULT_FEED_HOUR);
    _feedMinute = _feederPrefs.getInt("minute", FEEDER_DEFAULT_FEED_MINUTE);
    _durationMs = _feederPrefs.getULong("duration", FEEDER_DEFAULT_DURATION_MS);
    _feederPrefs.end();
}

static void _sanitizeConfig(void) {
    if (_feedDay < 0 || _feedDay > 7) {
        _feedDay = FEEDER_DEFAULT_FEED_DAY;
    }
    _feedHour = constrain(_feedHour, 0, 23);
    _feedMinute = constrain(_feedMinute, 0, 59);
    _durationMs = constrain(_durationMs, FEEDER_MIN_DURATION_MS, FEEDER_MAX_DURATION_MS);
    if (_commandSource != COMMAND_SOURCE_NETPIE && _commandSource != COMMAND_SOURCE_LOCAL_WEB) {
        _commandSource = FEEDER_DEFAULT_COMMAND_SOURCE;
    }
}

static void _writeOutput(bool enabled) {
#if FISH_FEEDER_PIN >= 0
    digitalWrite(FISH_FEEDER_PIN, enabled ? PUMP_ON : PUMP_OFF);
#else
    (void)enabled;
#endif
    _status.running = enabled;
}

static int _toWeekMinute(int day, int hour, int minute) {
    return (day * 24 * 60) + (hour * 60) + minute;
}

static bool _timeMatchesSchedule(const struct tm* timeinfo) {
    if (_feedDay == 7) {
        return timeinfo->tm_hour == _feedHour && timeinfo->tm_min == _feedMinute;
    }
    return timeinfo->tm_wday == _feedDay && timeinfo->tm_hour == _feedHour && timeinfo->tm_min == _feedMinute;
}

static void _markLastFeedTime(const struct tm* timeinfo) {
    if (timeinfo != NULL) {
        snprintf(_status.lastFeedAt, sizeof(_status.lastFeedAt), "%04d-%02d-%02d %02d:%02d",
                 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                 timeinfo->tm_hour, timeinfo->tm_min);
    }
}

bool fishFeederStartManualFeed(const char* reason) {
    if (!_hasOutput()) {
        _status.state = FEEDER_STATE_BLOCKED;
        _setReason("Set FISH_FEEDER_PIN when feeder wiring is finalized");
        return false;
    }
    if (_status.running) {
        _setReason("Feed request ignored because feeder is already running");
        return false;
    }

    _writeOutput(true);
    _feedStartMs = millis();
    _status.state = FEEDER_STATE_FEEDING;
    _setReason(reason != NULL ? reason : "Manual feed triggered");
    return true;
}

void fishFeederSetup(void) {
    _loadConfig();
    _sanitizeConfig();

#if FISH_FEEDER_PIN >= 0
    pinMode(FISH_FEEDER_PIN, OUTPUT);
    digitalWrite(FISH_FEEDER_PIN, PUMP_OFF);
#endif

    _status.hasOutput = _hasOutput();
    _status.state = _enabled ? FEEDER_STATE_IDLE : FEEDER_STATE_DISABLED;
    _setReason(_enabled ? "Waiting for next feed schedule" : "Fish feeder disabled");
}

void fishFeederLoop(void) {
    if (millis() - _lastLoopMs < FEEDER_CHECK_INTERVAL_MS) {
        return;
    }
    _lastLoopMs = millis();

    _status.hasOutput = _hasOutput();

    if (_status.running) {
        if (millis() - _feedStartMs >= _durationMs) {
            _writeOutput(false);
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 10)) {
                _markLastFeedTime(&timeinfo);
            }
            _status.state = _enabled ? FEEDER_STATE_IDLE : FEEDER_STATE_DISABLED;
            _setReason("Feed cycle completed");
        }
        return;
    }

    if (!_status.hasOutput) {
        _status.state = FEEDER_STATE_BLOCKED;
        _setReason("Set FISH_FEEDER_PIN when feeder wiring is finalized");
        return;
    }

    if (!_enabled) {
        _status.state = FEEDER_STATE_DISABLED;
        _setReason("Fish feeder disabled");
        return;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) {
        _status.state = FEEDER_STATE_BLOCKED;
        _setReason("Waiting for NTP time sync");
        return;
    }

    int currentWeekMinute = _toWeekMinute(timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min);
    if (_timeMatchesSchedule(&timeinfo) && _lastTriggeredWeekMinute != currentWeekMinute) {
        _lastTriggeredWeekMinute = currentWeekMinute;
        if (fishFeederStartManualFeed("Scheduled feed started")) {
            _markLastFeedTime(&timeinfo);
        }
        return;
    }

    _status.state = FEEDER_STATE_IDLE;
    _setReason("Waiting for next feed schedule");
}

void fishFeederGetConfig(FishFeederConfig* config) {
    if (config == NULL) {
        return;
    }
    config->commandSource = _commandSource;
    config->enabled = _enabled;
    config->feedDay = _feedDay;
    snprintf(config->feedTime, sizeof(config->feedTime), "%02d:%02d", _feedHour, _feedMinute);
    config->durationMs = _durationMs;
}

void fishFeederGetStatus(FishFeederStatus* status) {
    if (status != NULL) {
        *status = _status;
    }
}

void fishFeederSetCommandSource(CommandSource source) {
    _commandSource = source;
    _sanitizeConfig();
    _saveConfig();
    _setReason(source == COMMAND_SOURCE_NETPIE ? "Control source set to NETPIE" : "Control source set to Local Web");
}

CommandSource fishFeederGetCommandSource(void) {
    return _commandSource;
}

bool fishFeederAllowsNetpieControl(void) {
    return _commandSource == COMMAND_SOURCE_NETPIE;
}

bool fishFeederAllowsLocalControl(void) {
    return _commandSource == COMMAND_SOURCE_LOCAL_WEB;
}

void fishFeederSetEnabled(bool enabled) {
    _enabled = enabled;
    if (!_enabled && _status.running) {
        _writeOutput(false);
    }
    _saveConfig();
}

void fishFeederSetFeedDay(int day) {
    _feedDay = day;
    _sanitizeConfig();
    _saveConfig();
}

void fishFeederSetFeedTime(const char* timeStr) {
    if (timeStr == NULL || strlen(timeStr) < 4) {
        return;
    }
    _feedHour = atoi(timeStr);
    const char* colon = strchr(timeStr, ':');
    if (colon != NULL) {
        _feedMinute = atoi(colon + 1);
    }
    _sanitizeConfig();
    _saveConfig();
}

void fishFeederSetDurationMs(unsigned long durationMs) {
    _durationMs = durationMs;
    _sanitizeConfig();
    _saveConfig();
}

const char* fishFeederGetStateString(FishFeederState state) {
    switch (state) {
        case FEEDER_STATE_DISABLED:
            return "DISABLED";
        case FEEDER_STATE_IDLE:
            return "IDLE";
        case FEEDER_STATE_FEEDING:
            return "FEEDING";
        case FEEDER_STATE_BLOCKED:
            return "BLOCKED";
        default:
            return "UNKNOWN";
    }
}