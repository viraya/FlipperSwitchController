/**
 * Switch Controller - Flipper Zero FAP
 *
 * Emulates a Nintendo Switch Pro Controller over USB. Supports two modes:
 *   1. Manual Mode: Use Flipper's buttons to directly control the Switch
 *   2. Remote Mode: Receive commands over WiFi via ESP32 UART bridge
 *
 * Architecture: ViewDispatcher + SceneManager with 4 scenes:
 *   - SceneMenu: Mode selection (Manual / Remote / USB Debug)
 *   - SceneController: Live status display
 *   - SceneUsbDebug: USB handshake diagnostics
 *   - SceneConfirmExit: Confirm exit dialog
 *
 * Subsystems run independently of the GUI:
 *   - UART processing: FuriThread worker (uart_receiver.c)
 *   - Button engine: FuriTimer at 10ms periodic (100Hz)
 *   - USB HID reports: FuriTimer at 8ms (125Hz, in switch_pro_usb.c)
 *
 * Entry point: switch_controller_app
 */

#include <furi.h>
#include <furi_hal.h>
#include <string.h>
#include <stdlib.h>
#include "switch_controller_i.h"

#define TAG "SwitchController"

#define HEARTBEAT_TIMEOUT_MS 5000

/* Custom event IDs for cross-thread scene transitions */
#define CustomEventAutoStart 100

/* ---------- Forward Declarations: Scene Handlers ---------- */

/* Menu scene (implemented in scenes/scene_menu.c) */
void scene_menu_on_enter(void* context);
bool scene_menu_on_event(void* context, SceneManagerEvent event);
void scene_menu_on_exit(void* context);

/* USB debug scene (implemented in scenes/scene_usb_debug.c) */
void scene_usb_debug_on_enter(void* context);
bool scene_usb_debug_on_event(void* context, SceneManagerEvent event);
void scene_usb_debug_on_exit(void* context);

/* Controller scene (implemented in scenes/scene_controller.c) */
void scene_controller_on_enter(void* context);
bool scene_controller_on_event(void* context, SceneManagerEvent event);
void scene_controller_on_exit(void* context);

/* Confirm exit scene (implemented in scenes/scene_confirm_exit.c) */
void scene_confirm_exit_on_enter(void* context);
bool scene_confirm_exit_on_event(void* context, SceneManagerEvent event);
void scene_confirm_exit_on_exit(void* context);

/* ---------- Scene Handler Arrays ---------- */

static void (*const scene_on_enter_handlers[])(void*) = {
    scene_menu_on_enter,
    scene_usb_debug_on_enter,
    scene_controller_on_enter,
    scene_confirm_exit_on_enter,
};

static bool (*const scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    scene_menu_on_event,
    scene_usb_debug_on_event,
    scene_controller_on_event,
    scene_confirm_exit_on_event,
};

static void (*const scene_on_exit_handlers[])(void*) = {
    scene_menu_on_exit,
    scene_usb_debug_on_exit,
    scene_controller_on_exit,
    scene_confirm_exit_on_exit,
};

static const SceneManagerHandlers scene_handlers = {
    .on_enter_handlers = scene_on_enter_handlers,
    .on_event_handlers = scene_on_event_handlers,
    .on_exit_handlers = scene_on_exit_handlers,
    .scene_num = SceneCount,
};

/* ---------- ViewDispatcher Event Callbacks ---------- */

static bool app_custom_event_callback(void* context, uint32_t event) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool app_back_event_callback(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* ---------- Engine Timer Callback ---------- */

/**
 * Periodic timer callback at 10ms (100Hz).
 * Ticks the button engine and checks heartbeat timeout.
 * Runs in the FreeRTOS timer service thread.
 */
static void engine_timer_callback(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    /* Tick button engine (handle timed releases, macro steps, push USB state) */
    button_engine_tick(app->engine);

    /* Check if USB handshake completed — starts report timer from thread context
     * (can't start timers from USB ISR where handshake_complete is set) */
    switch_pro_usb_is_connected();

    /* Check heartbeat timeout: if WiFi was connected and heartbeats stopped */
    if(app->wifi_connected) {
        uint32_t now = furi_get_tick();
        if((now - app->last_heartbeat_tick) >
           furi_ms_to_ticks(HEARTBEAT_TIMEOUT_MS)) {
            app->wifi_connected = false;
            FURI_LOG_W(TAG, "Heartbeat timeout -- WiFi disconnected");

            /* Auto-pause on WiFi loss: release all buttons, notify PC */
            if(app->active && !app->paused) {
                app->paused = true;
                button_engine_release(app->engine, "ALL");
                uart_receiver_send(app->uart_receiver, "C:PAUSE\n");

                /* Update view model with WiFi lost state */
                with_view_model(
                    app->controller_view,
                    ControllerModel * m,
                    {
                        m->paused = true;
                        m->wifi_connected = false;
                        strncpy(
                            m->state_text,
                            "WiFi Lost",
                            sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    },
                    true);

                FURI_LOG_W(TAG, "Auto-paused due to WiFi loss");
            }
        }
    }

    /* Update controller status display at ~1Hz (every 100 ticks at 100Hz) */
    app->display_update_counter++;
    if(app->display_update_counter >= 100) {
        app->display_update_counter = 0;

        {
            uint32_t elapsed = 0;
            if(app->active) {
                elapsed = (furi_get_tick() - app->start_tick) /
                          furi_kernel_get_tick_frequency();
            }
            with_view_model(
                app->controller_view,
                ControllerModel * m,
                {
                    m->elapsed_seconds = elapsed;
                    m->wifi_connected = app->wifi_connected;
                    m->usb_connected = switch_pro_usb_is_connected();
                    m->action_count = app->action_counter;
                },
                true);
        }
    }
}

/* ---------- UART Command Callback ---------- */

/**
 * Called by the UART worker thread for each parsed command.
 * Dispatches to the button engine and tracks heartbeat timing.
 *
 * NOTE: Runs on the UART worker thread, not the main thread.
 * button_engine operations are safe here -- see uart_receiver.c header.
 */
static void on_uart_command(const UartCommand* cmd, void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    switch(cmd->type) {
    case UartCmdPress:
        if(button_engine_is_busy(app->engine)) {
            uart_receiver_send(app->uart_receiver, "ERR:BUSY\n");
        } else {
            button_engine_press(app->engine, cmd->arg1, cmd->duration_ms);
        }
        break;

    case UartCmdRelease:
        button_engine_release(app->engine, cmd->arg1);
        break;

    case UartCmdStick:
        if(button_engine_is_busy(app->engine)) {
            uart_receiver_send(app->uart_receiver, "ERR:BUSY\n");
        } else {
            button_engine_stick(
                app->engine, cmd->arg1, cmd->axis_x, cmd->axis_y, cmd->duration_ms);
        }
        break;

    case UartCmdMacro:
        if(!button_engine_macro(app->engine, cmd->arg1)) {
            uart_receiver_send(app->uart_receiver, "ERR:BUSY\n");
        }
        break;

    case UartCmdStatus: {
        /* Build and send status response */
        char status_response[64];
        snprintf(
            status_response,
            sizeof(status_response),
            "OK:STATUS:mode=%s:usb=%d\n",
            button_engine_is_busy(app->engine) ? "busy" : "idle",
            switch_pro_usb_is_connected() ? 1 : 0);
        uart_receiver_send(app->uart_receiver, status_response);
        break;
    }

    case UartCmdHeartbeat:
        /* H:PONG is already sent by uart_receiver worker thread.
         * Track heartbeat timing for WiFi status inference. */
        app->last_heartbeat_tick = furi_get_tick();
        if(!app->wifi_connected) {
            app->wifi_connected = true;
            FURI_LOG_I(TAG, "WiFi connected (heartbeat received)");

            /* Auto-transition to controller scene if still on menu */
            if(!app->active) {
                app->selected_mode = ControllerModeRemote;
                view_dispatcher_send_custom_event(
                    app->view_dispatcher, CustomEventAutoStart);
            }

            /* Update WiFi + USB indicators on controller view */
            with_view_model(
                app->controller_view,
                ControllerModel * m,
                {
                    m->wifi_connected = true;
                    m->usb_connected = switch_pro_usb_is_connected();
                    strncpy(m->state_text, "Connected", sizeof(m->state_text) - 1);
                    m->state_text[sizeof(m->state_text) - 1] = '\0';
                },
                true);
        }
        break;

    case UartCmdUpdate: {
        /* U:field:value -- PC-to-Flipper display update.
         * U:STATE:<text>   -- update state text
         * U:ACTIONS:142    -- update action counter */
        if(strcmp(cmd->arg1, "STATE") == 0) {
            /* Auto-transition to controller scene if still on menu */
            if(!app->active) {
                app->active = true;
                app->selected_mode = ControllerModeRemote;
                view_dispatcher_send_custom_event(
                    app->view_dispatcher, CustomEventAutoStart);
            }

            bool is_paused = (strcmp(cmd->arg2, "Paused") == 0);
            app->paused = is_paused;

            with_view_model(
                app->controller_view,
                ControllerModel * m,
                {
                    m->active = true;
                    m->paused = is_paused;
                    strncpy(m->state_text, cmd->arg2, sizeof(m->state_text) - 1);
                    m->state_text[sizeof(m->state_text) - 1] = '\0';
                },
                true);
        } else if(strcmp(cmd->arg1, "ACTIONS") == 0 ||
                  strcmp(cmd->arg1, "ATTEMPTS") == 0) {
            /* Parse action count */
            char* endptr = NULL;
            long count = strtol(cmd->arg2, &endptr, 10);
            if(endptr != cmd->arg2 && count >= 0) {
                app->action_counter = (uint32_t)count;
                with_view_model(
                    app->controller_view,
                    ControllerModel * m,
                    { m->action_count = (uint32_t)count; },
                    true);
            }
        }
        break;
    }

    case UartCmdUnknown:
        /* Already handled by uart_receiver worker with ERR response */
        break;
    }
}

/* ---------- App Lifecycle ---------- */

/**
 * Allocate and initialize the application.
 * Sets up ViewDispatcher, SceneManager, all GUI modules, and subsystems.
 */
static SwitchControllerApp* app_alloc(void) {
    SwitchControllerApp* app = malloc(sizeof(SwitchControllerApp));
    memset(app, 0, sizeof(SwitchControllerApp));

    FURI_LOG_I(TAG, "Switch Controller starting");

    /* GUI setup */
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&scene_handlers, app);

    /* ViewDispatcher event callbacks */
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, app_back_event_callback);

    /* Attach to GUI */
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Allocate Submenu module (menu scene) */
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewMenu, submenu_get_view(app->submenu));

    /* Allocate USB debug view */
    app->usb_debug_view = usb_debug_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewUsbDebug, app->usb_debug_view);

    /* Allocate custom controller view with app context for input callback */
    app->controller_view = controller_view_alloc(app);
    view_dispatcher_add_view(
        app->view_dispatcher, ViewController, app->controller_view);

    /* Allocate Widget module for confirm exit dialog */
    app->confirm_exit_widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewConfirmExit,
        widget_get_view(app->confirm_exit_widget));

    /* Create button engine */
    app->engine = button_engine_alloc();
    FURI_LOG_I(TAG, "Button engine created");

    /* Create and start UART receiver (launches worker thread) */
    app->uart_receiver = uart_receiver_alloc(on_uart_command, app);
    uart_receiver_start(app->uart_receiver);
    FURI_LOG_I(TAG, "UART receiver started");

    /* Start USB Pro Controller interface immediately */
    FURI_LOG_I(TAG, "Registering USB Pro Controller interface...");
    app->usb_started = switch_pro_usb_start();
    FURI_LOG_I(TAG, "USB result: %s", app->usb_started ? "OK" : "FAILED");

    /* Create engine timer (100Hz periodic for button engine ticking) */
    app->engine_timer = furi_timer_alloc(
        engine_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->engine_timer, furi_ms_to_ticks(10));

    /* Initialize heartbeat tracking */
    app->last_heartbeat_tick = furi_get_tick();
    app->wifi_connected = false;

    return app;
}

/**
 * Free the application and all associated resources.
 * Stops all subsystems and releases all memory.
 */
static void app_free(SwitchControllerApp* app) {
    FURI_LOG_I(TAG, "Switch Controller shutting down");

    /* Stop engine timer */
    furi_timer_stop(app->engine_timer);
    furi_timer_free(app->engine_timer);

    /* Release all buttons before stopping (idle state + delay for USB report) */
    SwitchButtonState idle_state;
    switch_pro_report_init(&idle_state);
    switch_pro_usb_set_button_state(&idle_state);
    furi_delay_ms(50);

    /* Stop UART receiver (signals worker thread, joins, releases serial) */
    FURI_LOG_I(TAG, "Stopping UART receiver...");
    uart_receiver_stop(app->uart_receiver);
    uart_receiver_free(app->uart_receiver);
    FURI_LOG_I(TAG, "UART receiver stopped");

    /* Free button engine */
    button_engine_free(app->engine);
    FURI_LOG_I(TAG, "Button engine freed");

    /* Stop USB Pro Controller and restore previous config */
    FURI_LOG_I(TAG, "Stopping USB Pro Controller...");
    switch_pro_usb_stop();
    FURI_LOG_I(TAG, "USB restored");

    /* Remove views from ViewDispatcher before freeing modules */
    view_dispatcher_remove_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewUsbDebug);
    view_dispatcher_remove_view(app->view_dispatcher, ViewController);
    view_dispatcher_remove_view(app->view_dispatcher, ViewConfirmExit);

    /* Free GUI modules */
    submenu_free(app->submenu);
    usb_debug_view_free(app->usb_debug_view);
    controller_view_free(app->controller_view);
    widget_free(app->confirm_exit_widget);

    /* Free SceneManager and ViewDispatcher */
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    /* Close GUI record */
    furi_record_close(RECORD_GUI);

    free(app);

    FURI_LOG_I(TAG, "Switch Controller exiting cleanly");
}

/* ---------- Main App Entry Point ---------- */

int32_t switch_controller_app(void* p) {
    UNUSED(p);

    SwitchControllerApp* app = app_alloc();

    /* Start at the menu scene */
    scene_manager_next_scene(app->scene_manager, SceneMenu);

    /* Run the ViewDispatcher event loop (blocks until exit) */
    view_dispatcher_run(app->view_dispatcher);

    app_free(app);
    return 0;
}
