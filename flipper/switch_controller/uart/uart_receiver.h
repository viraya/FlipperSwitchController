#pragma once

/**
 * UART receiver module for the Switch Controller FAP.
 *
 * Receives text commands from the ESP32 dev board over UART (pins 13/14)
 * at 115200 baud. Uses interrupt-safe async RX with FuriStreamBuffer
 * for buffering; a dedicated FuriThread worker processes buffered data
 * and dispatches parsed commands via callback.
 *
 * The worker thread is woken by the RX ISR via thread flags whenever
 * new bytes arrive, and blocks on FuriWaitForever otherwise.
 */

#include <furi.h>
#include "uart_protocol.h"

typedef struct UartReceiver UartReceiver;

/**
 * Callback invoked on the worker thread when a complete command is parsed.
 *
 * NOTE: This callback runs on the UART worker thread, not the main thread.
 * The callback must be safe to call from a non-main context. In practice,
 * the button engine operations (press/release/stick/macro) just set fields
 * and flip dirty flags, which is safe on the single-core Cortex-M4.
 *
 * @param cmd      Pointer to the parsed command (valid only during callback).
 * @param context  User context passed to uart_receiver_alloc().
 */
typedef void (*UartCommandCallback)(const UartCommand* cmd, void* context);

/**
 * Allocate and initialize a UART receiver.
 * @param callback  Function called for each parsed command.
 * @param context   User context passed to callback.
 * @return Allocated receiver (never NULL).
 */
UartReceiver* uart_receiver_alloc(UartCommandCallback callback, void* context);

/**
 * Free a UART receiver and all associated resources.
 * Must call uart_receiver_stop() first if started.
 * @param receiver  Receiver to free.
 */
void uart_receiver_free(UartReceiver* receiver);

/**
 * Start listening for UART data.
 * Acquires the USART serial handle, begins async RX, and launches
 * the worker thread that processes received bytes.
 * @param receiver  Receiver to start.
 */
void uart_receiver_start(UartReceiver* receiver);

/**
 * Stop listening for UART data.
 * Signals the worker thread to stop, joins it, then stops async RX,
 * deinitializes, and releases the serial handle.
 * @param receiver  Receiver to stop.
 */
void uart_receiver_stop(UartReceiver* receiver);

/**
 * Send a response string back over UART.
 * The string should be null-terminated; a newline is NOT appended
 * automatically -- include \n in the string.
 * @param receiver  Receiver (which owns the serial handle).
 * @param response  Null-terminated response string (e.g., "OK\n").
 */
void uart_receiver_send(UartReceiver* receiver, const char* response);

/**
 * Get the worker thread's FuriThreadId.
 * Used by the RX ISR to set thread flags for wakeup. This is stored
 * internally but exposed for diagnostics if needed.
 * @param receiver  Receiver instance.
 * @return FuriThreadId of the worker thread, or NULL if not started.
 */
FuriThreadId uart_receiver_get_thread_id(UartReceiver* receiver);
