#pragma once

/**
 * Custom view for the controller status display.
 *
 * Renders controller state, action counter, elapsed timer, WiFi indicator,
 * and control hints on the Flipper's 128x64 monochrome LCD.
 *
 * Manual mode: Flipper buttons map directly to Switch Pro Controller inputs.
 * Remote mode: Shows WiFi/UART status and received commands.
 *
 * Uses ViewModelTypeLockFree since all model fields are simple scalar
 * types (bool, uint32_t, char arrays) -- safe to update from timer
 * callback without mutex concerns on the single-core Cortex-M4.
 */

#include <gui/view.h>
#include <stdbool.h>
#include <stdint.h>

/* Model struct holding all display data */
typedef struct {
    bool active;
    bool paused;
    bool wifi_connected;
    bool usb_connected;
    bool manual_mode;
    uint32_t action_count;
    uint32_t elapsed_seconds;
    char state_text[20]; /* "Ready", "Active", "Paused", etc. */
    char mode_text[20];  /* "Manual (Flipper)", "Remote (WiFi)" */
} ControllerModel;

/**
 * Allocate the custom controller view.
 * @param app_context  Pointer to SwitchControllerApp (for input callback).
 * @return The View* to add to ViewDispatcher.
 */
View* controller_view_alloc(void* app_context);

/**
 * Free the custom controller view.
 * @param view  View returned by controller_view_alloc().
 */
void controller_view_free(View* view);
