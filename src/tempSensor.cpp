/**
 * @file tempSensor.cpp
 * @brief Implementation สำหรับ DS18B20 Temperature Sensor
 * @details ใช้ Async mode เพื่อให้เป็น Non-Blocking
 */

#include "tempSensor.h"
#include "logger.h"
#include <Preferences.h>


// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

// Max DS18B20 ที่ cache address ไว้สำหรับ scan/bind (mix + fish + เผื่อ)
#define TEMP_MAX_SCAN_DEVICES 4

static OneWire _oneWire(ONE_WIRE_PIN);
static DallasTemperature _sensors(&_oneWire);
static Preferences _tempPrefs;
static unsigned long _tempLastReadTime = 0;
static unsigned long _tempRequestTime = 0;
static float _lastWaterTemp[TEMP_CHANNEL_COUNT] = {NAN, NAN};
static DeviceAddress _boundAddresses[TEMP_CHANNEL_COUNT];
static bool _hasBoundAddress[TEMP_CHANNEL_COUNT] = {false, false};
static int _deviceCount = 0;

// Address ที่ scan เจอล่าสุด — cache ไว้ให้ command task อ่านได้โดยไม่แตะบัส
// (การแตะบัส OneWire ทั้งหมดทำใน TaskSensors เท่านั้น เพื่อกัน race เพราะบัสไม่มี mutex)
static DeviceAddress _scannedAddresses[TEMP_MAX_SCAN_DEVICES];
static int _scannedCount = 0;
static volatile bool _rescanRequested = false;

// State machine for async read
enum TempState { TEMP_IDLE, TEMP_WAITING };
static TempState _tempState = TEMP_IDLE;

// DS18B20 conversion time (750ms for 12-bit resolution) + safety margin
static const unsigned long CONVERSION_DELAY_MS = 800;
static uint8_t _retryCount = 0;
const uint8_t MAX_RETRIES = 3;

static const char* _channelKey(TempChannel channel) {
    return channel == TEMP_CHANNEL_FISH ? "addr_fish" : "addr_mix";
}

static const char* _channelName(TempChannel channel) {
    return channel == TEMP_CHANNEL_FISH ? "fish" : "mix";
}

static bool _isValidChannel(TempChannel channel) {
    return channel >= 0 && channel < TEMP_CHANNEL_COUNT;
}

static bool _addressEquals(const DeviceAddress a, const DeviceAddress b) {
    return memcmp(a, b, 8) == 0;
}

static void _formatAddress(const DeviceAddress addr, char* out, size_t outSize) {
    if (out == NULL || outSize == 0) {
        return;
    }
    if (outSize < 17) {
        out[0] = '\0';
        return;
    }
    snprintf(out, outSize, "%02X%02X%02X%02X%02X%02X%02X%02X",
             addr[0], addr[1], addr[2], addr[3],
             addr[4], addr[5], addr[6], addr[7]);
}

static void _saveAddress(TempChannel channel) {
    _tempPrefs.begin("tempSensor", false);
    _tempPrefs.putBytes(_channelKey(channel), _boundAddresses[channel], 8);
    _tempPrefs.end();
}

static bool _loadAddress(TempChannel channel) {
    _tempPrefs.begin("tempSensor", true);
    size_t len = _tempPrefs.getBytesLength(_channelKey(channel));
    bool ok = len == 8 && _tempPrefs.getBytes(_channelKey(channel), _boundAddresses[channel], 8) == 8;
    _tempPrefs.end();
    _hasBoundAddress[channel] = ok;
    return ok;
}

// ⚠️ ต้องเรียกใน TaskSensors context เท่านั้น (แตะบัส OneWire)
static void _performScan(void) {
    _sensors.begin();
    _sensors.setWaitForConversion(false); // คง Async mode หลัง re-init

    int count = _sensors.getDeviceCount();
    if (count < 0) {
        count = 0;
    }
    if (count > TEMP_MAX_SCAN_DEVICES) {
        count = TEMP_MAX_SCAN_DEVICES;
    }

    int cached = 0;
    for (int i = 0; i < count; i++) {
        DeviceAddress addr;
        if (_sensors.getAddress(addr, i)) {
            memcpy(_scannedAddresses[cached], addr, 8);
            cached++;
        }
    }

    _scannedCount = cached;
    _deviceCount = cached;
}

static bool _bindFromIndex(TempChannel channel, uint8_t index, bool save) {
    if (!_isValidChannel(channel) || (int)index >= _scannedCount) {
        return false;
    }

    memcpy(_boundAddresses[channel], _scannedAddresses[index], 8);
    _hasBoundAddress[channel] = true;
    if (save) {
        _saveAddress(channel);
    }
    return true;
}

static void _autoBindMissingAddresses(void) {
    if (_scannedCount <= 0) {
        return;
    }

    if (!_hasBoundAddress[TEMP_CHANNEL_MIX]) {
        _bindFromIndex(TEMP_CHANNEL_MIX, 0, true);
    }

    if (!_hasBoundAddress[TEMP_CHANNEL_FISH] && _scannedCount > 1) {
        uint8_t fishIndex = 1;
        for (int i = 0; i < _scannedCount; i++) {
            if (!_hasBoundAddress[TEMP_CHANNEL_MIX] ||
                !_addressEquals(_scannedAddresses[i], _boundAddresses[TEMP_CHANNEL_MIX])) {
                fishIndex = (uint8_t)i;
                break;
            }
        }
        _bindFromIndex(TEMP_CHANNEL_FISH, fishIndex, true);
    }
}

static bool _isInvalidTemp(float temp) {
    return temp == DEVICE_DISCONNECTED_C || temp == 85.0f || isnan(temp);
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void tempSetup(void) {
    // _performScan() จะเรียก begin() + ตั้ง Async mode + cache scanned addresses
    _performScan();

    _loadAddress(TEMP_CHANNEL_MIX);
    _loadAddress(TEMP_CHANNEL_FISH);
    _autoBindMissingAddresses();
    for (int ch = 0; ch < TEMP_CHANNEL_COUNT; ch++) {
        _lastWaterTemp[ch] = NAN;
    }
    _tempState = TEMP_IDLE;
    _retryCount = 0;

    if (_deviceCount == 0) {
        LOG_ERROR("DS18B20 not found! Check wiring.");
    } else {
        LOG_INFO("DS18B20 initialized - Found %d device(s) [Async Mode]", _deviceCount);
        for (int ch = 0; ch < TEMP_CHANNEL_COUNT; ch++) {
            if (_hasBoundAddress[ch]) {
                char addr[17];
                _formatAddress(_boundAddresses[ch], addr, sizeof(addr));
                LOG_INFO("DS18B20 %s bound to %s", _channelName((TempChannel)ch), addr);
            } else {
                LOG_WARN("DS18B20 %s channel not bound", _channelName((TempChannel)ch));
            }
        }
    }
}

float tempRead(void) {
    return tempGetTemperature(TEMP_CHANNEL_MIX);
}

float tempGetTemperature(TempChannel channel) {
    if (!_isValidChannel(channel)) {
        return NAN;
    }
    return _lastWaterTemp[channel];
}

int tempGetDeviceCount(void) {
    return _deviceCount;
}

int tempRefreshScan(void) {
    // ไม่แตะบัสจาก command task — ขอให้ TaskSensors ทำ rescan ในรอบถัดไป
    // (บัส OneWire ไม่มี mutex; ถ้า scan ที่นี่จะชนกับ requestTemperatures/getTempC ใน tempLoop)
    _rescanRequested = true;
    return _deviceCount;
}

bool tempGetScannedAddressHex(uint8_t index, char* out, size_t outSize) {
    if ((int)index >= _scannedCount) {
        return false;
    }
    _formatAddress(_scannedAddresses[index], out, outSize);
    return true;
}

bool tempGetBoundAddressHex(TempChannel channel, char* out, size_t outSize) {
    if (!_isValidChannel(channel) || !_hasBoundAddress[channel]) {
        return false;
    }
    _formatAddress(_boundAddresses[channel], out, outSize);
    return true;
}

bool tempBindChannelToIndex(TempChannel channel, uint8_t index) {
    bool ok = _bindFromIndex(channel, index, true);
    if (ok) {
        _lastWaterTemp[channel] = NAN;
    }
    return ok;
}

bool tempSwapChannels(void) {
    if (!_hasBoundAddress[TEMP_CHANNEL_MIX] || !_hasBoundAddress[TEMP_CHANNEL_FISH]) {
        return false;
    }

    DeviceAddress tmp;
    memcpy(tmp, _boundAddresses[TEMP_CHANNEL_MIX], 8);
    memcpy(_boundAddresses[TEMP_CHANNEL_MIX], _boundAddresses[TEMP_CHANNEL_FISH], 8);
    memcpy(_boundAddresses[TEMP_CHANNEL_FISH], tmp, 8);
    _saveAddress(TEMP_CHANNEL_MIX);
    _saveAddress(TEMP_CHANNEL_FISH);
    _lastWaterTemp[TEMP_CHANNEL_MIX] = NAN;
    _lastWaterTemp[TEMP_CHANNEL_FISH] = NAN;
    return true;
}

void tempLoop(void) {
    unsigned long currentTime = millis();
    
    switch (_tempState) {
        case TEMP_IDLE:
            // ทำ rescan ที่ command task ร้องขอ — ทำตอน IDLE เพื่อไม่ชน conversion
            if (_rescanRequested) {
                _rescanRequested = false;
                _performScan();
                _autoBindMissingAddresses();
            }
            // ถึงเวลาอ่านค่าใหม่หรือยัง?
            if (currentTime - _tempLastReadTime >= TEMP_READ_INTERVAL) {
                // ⭐ สั่ง request - ไม่ block เพราะ setWaitForConversion(false)
                _sensors.requestTemperatures();
                _tempRequestTime = currentTime;
                _tempState = TEMP_WAITING;
                _retryCount = 0; // Reset retry counter on new cycle
            }
            break;
            
        case TEMP_WAITING:
            // รอ conversion เสร็จ (800ms สำหรับ 12-bit including margin)
            if (currentTime - _tempRequestTime >= CONVERSION_DELAY_MS) {
                bool anyInvalid = false;
                
                for (int ch = 0; ch < TEMP_CHANNEL_COUNT; ch++) {
                    if (!_hasBoundAddress[ch]) {
                        _lastWaterTemp[ch] = NAN;
                        continue;
                    }

                    float temp = _sensors.getTempC(_boundAddresses[ch]);
                    if (_isInvalidTemp(temp)) {
                        anyInvalid = true;
                    } else {
                        _lastWaterTemp[ch] = temp;
                    }
                }

                if (anyInvalid) {
                    if (_retryCount < MAX_RETRIES) {
                        _retryCount++;
                        LOG_WARN("DS18B20 read fail, retrying... (%d/%d)", _retryCount, MAX_RETRIES);
                        
                        // Request again immediately
                        _sensors.requestTemperatures();
                        _tempRequestTime = currentTime;
                        // Stay in TEMP_WAITING
                        return; 
                    } else {
                        LOG_ERROR("Failed to read temperature from DS18B20 after %d retries", MAX_RETRIES);
                        for (int ch = 0; ch < TEMP_CHANNEL_COUNT; ch++) {
                            if (_hasBoundAddress[ch]) {
                                float temp = _sensors.getTempC(_boundAddresses[ch]);
                                if (_isInvalidTemp(temp)) {
                                    _lastWaterTemp[ch] = NAN;
                                }
                            }
                        }
                    }
                }
                
                _tempLastReadTime = currentTime;
                _tempState = TEMP_IDLE;
            }
            break;
    }
}
