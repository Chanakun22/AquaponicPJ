/**
 * @file telnetServer.cpp
 * @brief Telnet Server Implementation with Authentication
 */

#include "telnetServer.h"
#include "config.h"  // For SECRET_TELNET_PASSWORD
#include "commandHandler.h"
#include <WiFi.h>

WiFiServer _telnetServer(23);
WiFiClient _telnetClient;
bool _isAuthenticated = false;
static char _inputBuffer[64];
static size_t _bufferIndex = 0;



void telnetSetup(void) {
    _telnetServer.begin();
    _telnetServer.setNoDelay(true);
    memset(_inputBuffer, 0, sizeof(_inputBuffer));
    _bufferIndex = 0;
}

void telnetLoop(void) {
    // Check for new clients
    if (_telnetServer.hasClient()) {
        if (!_telnetClient || !_telnetClient.connected()) {
            if (_telnetClient) _telnetClient.stop();
            _telnetClient = _telnetServer.available();
            _isAuthenticated = false;
            memset(_inputBuffer, 0, sizeof(_inputBuffer));
            _bufferIndex = 0;
            
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
            
            // Handle Backspace
            if (c == 0x08 || c == 0x7F) {
                if (_bufferIndex > 0) {
                    _bufferIndex--;
                    _inputBuffer[_bufferIndex] = 0;
                    _telnetClient.print("\b \b"); // Erase character visually
                }
                continue;
            }

            // Handle newline (Command execution)
            if (c == '\n' || c == '\r') {
                if (_bufferIndex > 0) {
                    _telnetClient.println(); // New line for output
                    
                    if (!_isAuthenticated) {
                        // Password Check
                        if (strcmp(_inputBuffer, TELNET_PASSWORD) == 0) {
                            _isAuthenticated = true;
                            _telnetClient.println("Access Granted.");
                            _telnetClient.println("เข้าสู่ระบบเรียบร้อย✅");
                        } else {
                            _telnetClient.println("Access Denied.");
                            _telnetClient.print("Password: ");
                        }
                    } else {
                        // Command Processing
                        commandProcess(_inputBuffer, CMD_OUTPUT_TELNET);
                    }
                    
                    // Reset Buffer
                    memset(_inputBuffer, 0, sizeof(_inputBuffer));
                    _bufferIndex = 0;
                }
            } else if (isPrintable(c)) {
                // Add to buffer if space exists (leave 1 byte for null terminator)
                if (_bufferIndex < sizeof(_inputBuffer) - 1) {
                    _inputBuffer[_bufferIndex++] = c;
                    _inputBuffer[_bufferIndex] = 0; // Ensure null termination
                    
                    // Echo character (mask if password)
                    if (!_isAuthenticated) {
                        _telnetClient.print("*");
                    } else {
                        _telnetClient.print(c);
                    }
                }
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
