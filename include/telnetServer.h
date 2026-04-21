/**
 * @file telnetServer.h
 * @brief Telnet Server for Remote Debugging
 */

#ifndef TELNET_SERVER_H
#define TELNET_SERVER_H

#include <Arduino.h>

void telnetSetup(void);
void telnetLoop(void);
size_t telnetPrintf(const char *format, ...);
size_t telnetPrintfNonBlocking(const char *format, ...);
bool telnetIsConnected(void);

#endif // TELNET_SERVER_H
