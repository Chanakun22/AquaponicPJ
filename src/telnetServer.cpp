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
                            _telnetClient.println("เข้าสู่ระบบเรียบร้อย✅");
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
            // Authenticated command handling
            else {
                if (c == '\n' || c == '\r') {
                    if (_inputBuffer.length() > 0) {
                        // Convert String to char array for commandProcess
                        char cmdBuf[64];
                        _inputBuffer.toCharArray(cmdBuf, sizeof(cmdBuf));
                        
                        // Process command via commandHandler
                        commandProcess(cmdBuf, CMD_OUTPUT_TELNET);
                        
                        _inputBuffer = "";
                    }
                } else if (isPrintable(c)) {
                    _inputBuffer += c;
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
