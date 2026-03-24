/**
 * UART receiver implementation with dedicated worker thread.
 *
 * Async RX callback runs in interrupt context and pushes raw bytes
 * to a FuriStreamBuffer with zero timeout (never blocks). After each
 * byte, it sets the WorkerEvtRxReady flag on the worker thread.
 *
 * The worker thread waits on thread flags (RxReady or Stop). On RxReady,
 * it reads from the stream buffer, accumulates a line buffer, and parses
 * complete lines into UartCommand structs. Parsed commands are dispatched
 * via callback directly on the worker thread.
 *
 * Uses FuriHalSerialIdUsart (GPIO pins 13 TX / 14 RX) which connects
 * to the ESP32-S2 WiFi dev board on the Flipper's GPIO header.
 *
 * Thread safety note: The command callback is invoked on the worker thread.
 * The callback calls button_engine_press/release/stick/macro which set
 * fields and flip dirty flags. The engine_timer callback (FuriTimer, 10ms)
 * calls button_engine_tick() which reads these fields. On the single-core
 * Cortex-M4, these operations are effectively atomic for scalar fields
 * (bool, uint32_t) at the same thread priority. A FuriMutex could be added
 * for strict correctness but is not needed for the current field sizes.
 */

#include "uart_receiver.h"
#include <furi.h>
#include <furi_hal.h>

#define TAG "UartReceiver"

/* Worker thread event flags */
#define WorkerEvtRxReady (1 << 0)
#define WorkerEvtStop    (1 << 1)
#define WorkerEvtAll     (WorkerEvtRxReady | WorkerEvtStop)

struct UartReceiver {
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    UartCommandCallback callback;
    void* callback_context;

    /* Line buffer for accumulating characters until \n */
    char line_buf[UART_CMD_MAX_LEN];
    size_t line_pos;

    bool started;

    /* Worker thread */
    FuriThread* worker_thread;
    FuriThreadId worker_thread_id;
};

/* ---------- Command Parser ---------- */

/**
 * Parse a null-terminated line into a UartCommand.
 * Strips trailing \r if present.
 * Returns true on successful parse, false for unknown/malformed commands.
 */
static bool parse_uart_command(const char* line, UartCommand* cmd) {
    /* Initialize command to safe defaults */
    memset(cmd, 0, sizeof(UartCommand));
    cmd->type = UartCmdUnknown;

    if(line == NULL || line[0] == '\0') return false;

    /* Work on a mutable copy for parsing */
    char buf[UART_CMD_MAX_LEN];
    size_t len = strlen(line);
    if(len >= UART_CMD_MAX_LEN) return false;
    memcpy(buf, line, len + 1);

    /* Strip trailing \r (in case of \r\n line endings) */
    if(len > 0 && buf[len - 1] == '\r') {
        buf[len - 1] = '\0';
        len--;
    }

    if(len == 0) return false;

    /* Identify command type by first character */
    char cmd_char = buf[0];

    /* Verify colon separator */
    if(len < 2 || buf[1] != ':') return false;

    const char* payload = &buf[2]; /* Everything after "X:" */

    switch(cmd_char) {
    case 'P': {
        /* P:buttons:duration_ms (e.g., P:A:100, P:ABXY:200) */
        cmd->type = UartCmdPress;

        /* Find the second colon separating buttons from duration */
        const char* colon = strchr(payload, ':');
        if(colon == NULL) return false;

        size_t btn_len = (size_t)(colon - payload);
        if(btn_len == 0 || btn_len >= sizeof(cmd->arg1)) return false;

        memcpy(cmd->arg1, payload, btn_len);
        cmd->arg1[btn_len] = '\0';

        /* Parse duration */
        char* endptr = NULL;
        long duration = strtol(colon + 1, &endptr, 10);
        if(endptr == colon + 1 || duration < 0) return false;
        cmd->duration_ms = (uint32_t)duration;

        return true;
    }

    case 'R': {
        /* R:buttons or R:ALL (e.g., R:A, R:ALL) */
        cmd->type = UartCmdRelease;

        size_t plen = strlen(payload);
        if(plen == 0 || plen >= sizeof(cmd->arg1)) return false;

        memcpy(cmd->arg1, payload, plen + 1);
        return true;
    }

    case 'S': {
        /* S:stick:x:y:duration_ms (e.g., S:L:0:2048:500) */
        cmd->type = UartCmdStick;

        /* Parse stick identifier */
        const char* p = payload;
        const char* colon1 = strchr(p, ':');
        if(colon1 == NULL) return false;

        size_t stick_len = (size_t)(colon1 - p);
        if(stick_len == 0 || stick_len >= sizeof(cmd->arg1)) return false;
        memcpy(cmd->arg1, p, stick_len);
        cmd->arg1[stick_len] = '\0';

        /* Parse X axis */
        p = colon1 + 1;
        char* endptr = NULL;
        long x = strtol(p, &endptr, 10);
        if(endptr == p || *endptr != ':') return false;
        cmd->axis_x = (int16_t)x;

        /* Parse Y axis */
        p = endptr + 1;
        long y = strtol(p, &endptr, 10);
        if(endptr == p || *endptr != ':') return false;
        cmd->axis_y = (int16_t)y;

        /* Parse duration */
        p = endptr + 1;
        long dur = strtol(p, &endptr, 10);
        if(endptr == p || dur < 0) return false;
        cmd->duration_ms = (uint32_t)dur;

        return true;
    }

    case 'M': {
        /* M:macro_name (e.g., M:SOFT_RESET) */
        cmd->type = UartCmdMacro;

        size_t plen = strlen(payload);
        if(plen == 0 || plen >= sizeof(cmd->arg1)) return false;

        memcpy(cmd->arg1, payload, plen + 1);
        return true;
    }

    case 'Q': {
        /* Q:STATUS */
        cmd->type = UartCmdStatus;
        return true;
    }

    case 'H': {
        /* H:PING */
        cmd->type = UartCmdHeartbeat;
        return true;
    }

    case 'U': {
        /* U:field:value (PC-to-Flipper display update)
         * U:STATE:Active   -- update state text
         * U:STATE:Paused
         * U:STATE:Done
         * U:ACTIONS:142    -- update action counter */
        cmd->type = UartCmdUpdate;

        /* Parse field name (before second colon) */
        const char* colon = strchr(payload, ':');
        if(colon == NULL) return false;

        size_t field_len = (size_t)(colon - payload);
        if(field_len == 0 || field_len >= sizeof(cmd->arg1)) return false;

        memcpy(cmd->arg1, payload, field_len);
        cmd->arg1[field_len] = '\0';

        /* Parse value (after second colon) */
        const char* value = colon + 1;
        size_t val_len = strlen(value);
        if(val_len == 0 || val_len >= sizeof(cmd->arg2)) return false;

        memcpy(cmd->arg2, value, val_len + 1);
        return true;
    }

    default:
        return false;
    }
}

/* ---------- Internal Line Processing ---------- */

/**
 * Process buffered UART bytes from the stream buffer.
 * Called by the worker thread when WorkerEvtRxReady is signaled.
 * Reads from the stream buffer, parses complete lines into commands,
 * and invokes the callback for each valid command.
 */
static void uart_receiver_process_internal(UartReceiver* receiver) {
    if(receiver == NULL || !receiver->started) return;

    /* Read available bytes from the stream buffer (non-blocking) */
    uint8_t byte;
    while(furi_stream_buffer_receive(receiver->rx_stream, &byte, 1, 0) > 0) {
        if(byte == '\n') {
            /* Complete line received -- null-terminate and parse */
            receiver->line_buf[receiver->line_pos] = '\0';

            if(receiver->line_pos > 0) {
                UartCommand cmd;
                if(parse_uart_command(receiver->line_buf, &cmd)) {
                    /* Handle heartbeat directly (respond immediately) */
                    if(cmd.type == UartCmdHeartbeat) {
                        uart_receiver_send(receiver, "H:PONG\n");
                    }

                    /* Dispatch to callback */
                    receiver->callback(&cmd, receiver->callback_context);

                    /* Send OK for non-heartbeat, non-status, non-update commands */
                    if(cmd.type != UartCmdHeartbeat && cmd.type != UartCmdStatus &&
                       cmd.type != UartCmdUpdate) {
                        uart_receiver_send(receiver, "OK\n");
                    }
                } else {
                    uart_receiver_send(receiver, "ERR:UNKNOWN_CMD\n");
                    FURI_LOG_W(TAG, "Unknown command: %s", receiver->line_buf);
                }
            }

            /* Reset line buffer */
            receiver->line_pos = 0;
        } else if(receiver->line_pos < UART_CMD_MAX_LEN - 1) {
            /* Accumulate character */
            receiver->line_buf[receiver->line_pos++] = (char)byte;
        } else {
            /* Buffer overflow -- discard line */
            uart_receiver_send(receiver, "ERR:OVERFLOW\n");
            FURI_LOG_W(TAG, "Line buffer overflow, discarding");
            receiver->line_pos = 0;
        }
    }
}

/* ---------- Interrupt-Context RX Callback ---------- */

/**
 * CRITICAL: This runs in interrupt context.
 * ONLY read the byte, push to stream buffer, and signal worker thread.
 * Do NOT: allocate memory, lock mutex, call FURI_LOG, parse strings,
 * or call any function that might block.
 */
static void uart_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    UartReceiver* receiver = (UartReceiver*)context;

    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(receiver->rx_stream, &byte, 1, 0);

        /* Wake worker thread to process the new byte */
        furi_thread_flags_set(receiver->worker_thread_id, WorkerEvtRxReady);
    }
}

/* ---------- Worker Thread ---------- */

/**
 * Worker thread entry point.
 * Waits for RxReady or Stop flags. On RxReady, processes buffered bytes.
 * On Stop, breaks out of the loop and returns.
 */
static int32_t uart_worker_thread(void* context) {
    UartReceiver* receiver = (UartReceiver*)context;
    FURI_LOG_I(TAG, "UART worker thread started");

    while(true) {
        uint32_t flags = furi_thread_flags_wait(
            WorkerEvtAll,
            FuriFlagWaitAny,
            FuriWaitForever);

        /* Check for errors in flag wait */
        if(flags & FuriFlagError) {
            FURI_LOG_E(TAG, "Worker thread flag error");
            break;
        }

        if(flags & WorkerEvtStop) {
            FURI_LOG_I(TAG, "UART worker thread stopping");
            break;
        }

        if(flags & WorkerEvtRxReady) {
            uart_receiver_process_internal(receiver);
        }
    }

    FURI_LOG_I(TAG, "UART worker thread exiting");
    return 0;
}

/* ---------- Public API ---------- */

UartReceiver* uart_receiver_alloc(UartCommandCallback callback, void* context) {
    furi_check(callback != NULL);

    UartReceiver* receiver = malloc(sizeof(UartReceiver));
    memset(receiver, 0, sizeof(UartReceiver));

    receiver->callback = callback;
    receiver->callback_context = context;
    receiver->rx_stream = furi_stream_buffer_alloc(UART_RX_BUF_SIZE, 1);
    receiver->line_pos = 0;
    receiver->started = false;
    receiver->serial = NULL;
    receiver->worker_thread = NULL;
    receiver->worker_thread_id = NULL;

    return receiver;
}

void uart_receiver_free(UartReceiver* receiver) {
    if(receiver == NULL) return;

    if(receiver->started) {
        uart_receiver_stop(receiver);
    }

    if(receiver->rx_stream) {
        furi_stream_buffer_free(receiver->rx_stream);
        receiver->rx_stream = NULL;
    }

    free(receiver);
}

void uart_receiver_start(UartReceiver* receiver) {
    furi_check(receiver != NULL);
    if(receiver->started) return;

    /* Acquire USART serial handle (GPIO pins 13 TX / 14 RX) */
    receiver->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(receiver->serial == NULL) {
        FURI_LOG_E(TAG, "Failed to acquire USART -- is another app using GPIO 13/14?");
        return;
    }

    /* Initialize with baud rate */
    furi_hal_serial_init(receiver->serial, UART_BAUD_RATE);

    /* Create and start worker thread before enabling RX interrupts */
    receiver->worker_thread = furi_thread_alloc_ex(
        "uart_worker", 1024, uart_worker_thread, receiver);
    furi_thread_start(receiver->worker_thread);
    receiver->worker_thread_id = furi_thread_get_id(receiver->worker_thread);

    /* Start async RX with interrupt callback */
    furi_hal_serial_async_rx_start(receiver->serial, uart_rx_callback, receiver, false);

    receiver->started = true;
    receiver->line_pos = 0;

    FURI_LOG_I(TAG, "UART receiver started (baud=%lu)", (unsigned long)UART_BAUD_RATE);
}

void uart_receiver_stop(UartReceiver* receiver) {
    if(receiver == NULL || !receiver->started) return;

    /* Stop async RX first (no more ISR callbacks) */
    furi_hal_serial_async_rx_stop(receiver->serial);

    /* Signal worker thread to stop and wait for it */
    if(receiver->worker_thread) {
        furi_thread_flags_set(receiver->worker_thread_id, WorkerEvtStop);
        furi_thread_join(receiver->worker_thread);
        furi_thread_free(receiver->worker_thread);
        receiver->worker_thread = NULL;
        receiver->worker_thread_id = NULL;
    }

    /* Deinitialize serial */
    furi_hal_serial_deinit(receiver->serial);

    /* Release serial handle */
    furi_hal_serial_control_release(receiver->serial);
    receiver->serial = NULL;

    receiver->started = false;

    FURI_LOG_I(TAG, "UART receiver stopped");
}

void uart_receiver_send(UartReceiver* receiver, const char* response) {
    if(receiver == NULL || receiver->serial == NULL || response == NULL) return;

    size_t len = strlen(response);
    if(len > 0) {
        furi_hal_serial_tx(receiver->serial, (const uint8_t*)response, len);
    }
}

FuriThreadId uart_receiver_get_thread_id(UartReceiver* receiver) {
    if(receiver == NULL) return NULL;
    return receiver->worker_thread_id;
}
