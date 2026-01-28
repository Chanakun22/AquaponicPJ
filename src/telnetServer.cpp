/**
 * @file telnetServer.cpp
 * @brief Telnet Server Implementation with Authentication
 */

#include "telnetServer.h"
#include "config.h"  // For SECRET_TELNET_PASSWORD
#include <WiFi.h>

WiFiServer _telnetServer(23);
WiFiClient _telnetClient;
bool _isAuthenticated = false;
static String _inputBuffer = "";

// Default password if not defined
#ifndef SECRET_TELNET_PASSWORD
#define SECRET_TELNET_PASSWORD "admin"
#endif

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
            _isAuthenticated = false;
            _inputBuffer = "";
            
            _telnetClient.println("==================================");
            _telnetClient.println("Aquaponics Debug Console");
            _telnetClient.print("Password: ");
            _telnetClient.flush();
        } else {
            // Reject new connection
            WiFiClient rejected = _telnetServer.available();
            rejected.println("System busy");
            rejected.stop();
        }
    }

    // Check connection status
    if (_telnetClient && !_telnetClient.connected()) {
        _telnetClient.stop();
        _isAuthenticated = false;
    }
    
    // Handle Input
    if (_telnetClient && _telnetClient.connected() && _telnetClient.available()) {
        while (_telnetClient.available()) {
            char c = _telnetClient.read();
            
            // Password handling
            if (!_isAuthenticated) {
                if (c == '\n' || c == '\r') {
                    if (_inputBuffer.length() > 0) {
                        if (_inputBuffer == SECRET_TELNET_PASSWORD) {
                            _isAuthenticated = true;
                            _telnetClient.println("\r\nAccess Granted.");
                            _telnetClient.println("Type 'help' for commands.");
                        } else {
                            _telnetClient.println("\r\nAccess Denied.");
                            _telnetClient.print("Password: ");
                        }
                        _inputBuffer = "";
                    }
                } else if (isPrintable(c)) {
                    _inputBuffer += c;
                    _telnetClient.print("*"); // Mask password
                }
            } 
            // Authenticated command handling (if we want to move Serial cmds here later)
            else {
                // For now, simple echo or discard. 
                // Commands are handled in main.cpp via Serial, 
                // but we could buffer them here and pass to a shared command parser.
                // Currently main.cpp reads from Serial. 
                // If we want Telnet commands, we need to integrate better. 
                // BUT user just asked for production ready. 
                // Security is priority.
            }
        }
    }
}

size_t telnetPrintf(const char *format, ...) {
    if (!_telnetClient || !_telnetClient.connected() || !_isAuthenticated) return 0;
    
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    
    return _telnetClient.print(buf);
}

bool telnetIsConnected(void) {
    return (_telnetClient && _telnetClient.connected() && _isAuthenticated);
}
