/**
 * @file commandHandler.h
 * @brief Command Handler Module - รวมคำสั่งทั้งหมดไว้ที่เดียว
 * @details ใช้ร่วมกันระหว่าง Serial และ Telnet
 */

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>

/**
 * @brief Output target for command responses
 */
typedef enum {
    CMD_OUTPUT_SERIAL,
    CMD_OUTPUT_TELNET,
    CMD_OUTPUT_BOTH
} CommandOutput_t;

/**
 * @brief Initialize command handler
 */
void commandSetup(void);

/**
 * @brief Process a command string
 * @param cmd Command string to process (will be modified)
 * @param output Where to send the response
 */
void commandProcess(char* cmd, CommandOutput_t output);

/**
 * @brief Print formatted output to specified target
 * @param output Target (Serial/Telnet/Both)
 * @param format Printf-style format string
 */
void commandPrintf(CommandOutput_t output, const char* format, ...);

/**
 * @brief Check and process Serial input
 */
void commandCheckSerial(void);

/**
 * @brief Check and process Telnet input
 */
void commandCheckTelnet(void);

/**
 * @brief Non-blocking pump test auto-off tick (call in loop)
 */
void commandPumpTestTick(void);

#endif // COMMAND_HANDLER_H
