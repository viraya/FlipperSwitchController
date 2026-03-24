#pragma once

/**
 * Button engine for timed press/release and macro execution.
 *
 * The engine translates high-level commands (press A for 100ms,
 * execute SOFT_RESET macro) into SwitchButtonState updates that
 * the USB layer reads at 125Hz.
 *
 * Call button_engine_tick() every ~10ms from the main loop.
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct ButtonEngine ButtonEngine;

/**
 * Allocate a new button engine. Initial state: all released, sticks centered.
 * @return Allocated engine (never NULL).
 */
ButtonEngine* button_engine_alloc(void);

/**
 * Free a button engine and all associated resources.
 * @param engine  Engine to free.
 */
void button_engine_free(ButtonEngine* engine);

/**
 * Press specified buttons for duration_ms, then auto-release.
 * @param engine       Engine instance.
 * @param buttons_str  Button names: "A", "B", "ABXY", "UP", "DOWN", "LEFT",
 *                     "RIGHT", "L", "R", "ZL", "ZR", "PLUS", "MINUS",
 *                     "HOME", "LSTICK", "RSTICK". Single chars can be
 *                     combined: "ABXY" presses A, B, X, Y simultaneously.
 * @param duration_ms  How long to hold before auto-release.
 */
void button_engine_press(ButtonEngine* engine, const char* buttons_str, uint32_t duration_ms);

/**
 * Release specified buttons immediately.
 * @param engine       Engine instance.
 * @param buttons_str  Button names, or "ALL" to release everything.
 */
void button_engine_release(ButtonEngine* engine, const char* buttons_str);

/**
 * Set stick position for duration_ms, then return to center.
 * @param engine       Engine instance.
 * @param stick        "L" for left stick, "R" for right stick.
 * @param x            X axis value (0-4095, center=2048).
 * @param y            Y axis value (0-4095, center=2048).
 * @param duration_ms  How long to hold position before centering.
 */
void button_engine_stick(
    ButtonEngine* engine,
    const char* stick,
    int16_t x,
    int16_t y,
    uint32_t duration_ms);

/**
 * Start executing a predefined macro sequence.
 * While a macro is running, individual press commands are rejected
 * (returns false via engine busy state).
 * @param engine      Engine instance.
 * @param macro_name  Macro name (e.g., "SOFT_RESET", "PRESS_A").
 * @return true if macro found and started, false if not found or busy.
 */
bool button_engine_macro(ButtonEngine* engine, const char* macro_name);

/**
 * Tick the engine -- call every ~10ms from the main loop.
 * Updates timed releases, advances macro steps, and pushes
 * state to USB via switch_pro_usb_set_button_state().
 * @param engine  Engine instance.
 */
void button_engine_tick(ButtonEngine* engine);

/**
 * Returns true if a macro is currently executing or timed actions are active.
 * @param engine  Engine instance.
 * @return true if busy, false if idle.
 */
bool button_engine_is_busy(ButtonEngine* engine);

/**
 * Get a human-readable status string for display.
 * @param engine  Engine instance.
 * @return Static string describing current state (valid until next tick).
 */
const char* button_engine_get_status(ButtonEngine* engine);
