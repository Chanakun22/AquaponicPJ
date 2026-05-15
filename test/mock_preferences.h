/**
 * @file mock_preferences.h
 * @brief Mock Preferences class for native (PC) unit testing
 * @details แทนที่ ESP32 Preferences.h — เก็บค่าใน RAM ชั่วคราว
 */

#ifndef MOCK_PREFERENCES_H
#define MOCK_PREFERENCES_H

#include <map>
#include <string>
#include <cstring>
#include <cstdint>

class Preferences {
private:
    std::map<std::string, std::map<std::string, std::string>> _store;
    std::string _currentNamespace;
    bool _opened = false;

public:
    bool begin(const char* name, bool readOnly) {
        _currentNamespace = name;
        _opened = true;
        return true;
    }

    void end() {
        _opened = false;
        _currentNamespace.clear();
    }

    bool clear() {
        auto it = _store.find(_currentNamespace);
        if (it != _store.end()) {
            _store.erase(it);
        }
        return true;
    }

    bool remove(const char* key) {
        if (!_opened) return false;
        auto ns = _store.find(_currentNamespace);
        if (ns != _store.end()) {
            ns->second.erase(key);
        }
        return true;
    }

    bool isKey(const char* key) {
        if (!_opened) return false;
        auto ns = _store.find(_currentNamespace);
        if (ns == _store.end()) return false;
        return ns->second.find(key) != ns->second.end();
    }

    // ============ typed getters ============

    int getInt(const char* key, int defaultValue = 0) {
        if (!_opened) return defaultValue;
        auto ns = _store.find(_currentNamespace);
        if (ns == _store.end()) return defaultValue;
        auto kv = ns->second.find(key);
        if (kv == ns->second.end()) return defaultValue;
        return std::stoi(kv->second);
    }

    float getFloat(const char* key, float defaultValue = 0.0f) {
        if (!_opened) return defaultValue;
        auto ns = _store.find(_currentNamespace);
        if (ns == _store.end()) return defaultValue;
        auto kv = ns->second.find(key);
        if (kv == ns->second.end()) return defaultValue;
        return std::stof(kv->second);
    }

    bool getBool(const char* key, bool defaultValue = false) {
        if (!_opened) return defaultValue;
        auto ns = _store.find(_currentNamespace);
        if (ns == _store.end()) return defaultValue;
        auto kv = ns->second.find(key);
        if (kv == ns->second.end()) return defaultValue;
        return kv->second == "1" || kv->second == "true";
    }

    // ============ typed putters ============

    size_t putInt(const char* key, int value) {
        if (!_opened) return 0;
        _store[_currentNamespace][key] = std::to_string(value);
        return sizeof(int);
    }

    size_t putFloat(const char* key, float value) {
        if (!_opened) return 0;
        _store[_currentNamespace][key] = std::to_string(value);
        return sizeof(float);
    }

    size_t putBool(const char* key, bool value) {
        if (!_opened) return 0;
        _store[_currentNamespace][key] = value ? "1" : "0";
        return sizeof(bool);
    }
};

#endif // MOCK_PREFERENCES_H