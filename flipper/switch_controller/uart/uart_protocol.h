#pragma once

/**
 * UART command protocol definitions for Flipper <-> ESP32 communication.
 *
 * Protocol: newline-delimited text commands at 115200 baud.
 *
 * Commands (ESP32 -> Flipper):
 *   P:buttons:duration_ms   Press buttons for duration (e.g., P:A:100)
 *   R:buttons               Release buttons (e.g., R:A, R:ALL)
 *   S:stick:x:y:duration_ms Set stick position (e.g., S:L:0:2048:500)
 *   M:macro_name            Execute macro (e.g., M:SOFT_RESET)
 *   Q:STATUS                Query current status
 *   H:PING                  Heartbeat ping
 *   U:field:value           Display update from PC (e.g., U:STATE:Hunting, U:ATTEMPTS:42)
 *
 * Responses (Flipper -> ESP32):
 *   OK\n                          Command acknowledged
 *   OK:STATUS:mode=X:usb=Y\n     Status response
 *   H:PONG\n                      Heartbeat response
 *   ERR:UNKNOWN_CMD\n             Unknown command
 *   ERR:OVERFLOW\n                Line buffer overflow
 *   ERR:PARSE\n                   Parse error
 *   ERR:BUSY\n                    Engine busy (macro running)
 */

#include <stdint.h>
#include <stdbool.h>

/* Protocol constants */
#define UART_BAUD_RATE    115200
#define UART_CMD_MAX_LEN  64
#define UART_RX_BUF_SIZE  256

/* Command types */
typedef enum {
    UartCmdPress,      // P:buttons:duration_ms
    UartCmdRelease,    // R:buttons or R:ALL
    UartCmdStick,      // S:stick:x:y:duration_ms
    UartCmdMacro,      // M:macro_name
    UartCmdStatus,     // Q:STATUS
    UartCmdHeartbeat,  // H:PING
    UartCmdUpdate,     // U:field:value (PC-to-Flipper display update)
    UartCmdUnknown,
} UartCommandType;

/* Parsed command struct */
typedef struct {
    UartCommandType type;
    char arg1[16];        // Button names, macro name, stick id, or update field
    char arg2[16];        // Update value (for U: commands)
    uint32_t duration_ms; // For timed commands
    int16_t axis_x;       // For stick commands
    int16_t axis_y;       // For stick commands
} UartCommand;
