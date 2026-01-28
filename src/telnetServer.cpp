/**
 * @file telnetServer.cpp
 * @brief Telnet Server Implementation
 */

#include "telnetServer.h"
#include <WiFi.h>

WiFiServer _telnetServer(23);
WiFiClient _telnetClient;
bool _isClientConnected = false;

void telnetSetup(void) {
    _telnetServer.begin();
    _telnetServer.setNoDelay(true);
}

void telnetLoop(void) {
    // Check for new clients
    if (_telnetServer.hasClient()) {
        if (!_telnetClient || !_telnetClient.connected()) {
            if (_telnetClient) _telnetClient.stop();
            _telnetClient = _telnetServer.available();
            _telnetClient.println("==================================");
            _telnetClient.println("Connected to Aquaponics Telnet Debug");
            _telnetClient.println("==================================");
            _telnetClient.flush();
            _isClientConnected = true;
        } else {
            // Reject new connection if one exists
            WiFiClient rejected = _telnetServer.available();
            rejected.println("Server busy");
            rejected.stop();
        }
    }

    // Check client status
    if (_telnetClient && !_telnetClient.connected()) {
        _telnetClient.stop();
        _isClientConnected = false;
    }
    
    // Simple echo/input handling can be added here if needed
    if (_telnetClient && _telnetClient.available()) {
        while(_telnetClient.available()) _telnetClient.read(); // Discard input for now
    }
}

size_t telnetPrintf(const char *format, ...) {
    if (!_telnetClient || !_telnetClient.connected()) return 0;
    
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    
    return _telnetClient.print(buf);
}

bool telnetIsConnected(void) {
    return (_telnetClient && _telnetClient.connected());
}
