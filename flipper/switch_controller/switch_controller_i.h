#pragma once

/**
 * Switch Controller - Internal header
 *
 * Shared app struct and enums used across all scenes.
 * Include this from scene files and the main app file.
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>

#include "uart/uart_receiver.h"
#include "uart/uart_protocol.h"
#include "engine/button_engine.h"
#include "usb/switch_pro_usb.h"
#include "usb/switch_pro_report.h"
#include "scenes/scene_config.h"
#include "views/usb_debug_view.h"
#include "views/controller_view.h"

/* View IDs for ViewDispatcher */
typedef enum {
    ViewMenu,
    ViewUsbDebug,
    ViewController,
    ViewConfirmExit,
} AppView;

/* Controller mode selection */
typedef enum {
    ControllerModeManual = 0,    /* Use Flipper buttons directly (no ESP32) */
    ControllerModeRemote,        /* WiFi remote control via ESP32 bridge */
    ControllerModeCount,
} ControllerMode;

/* App context shared across all scenes */
typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    /* GUI modules */
    Submenu* submenu;
    View* usb_debug_view;         /* USB debug/connect page */
    View* controller_view;        /* Controller status view */
    Widget* confirm_exit_widget;  /* Exit confirmation dialog */

    /* Subsystems (lifetime: entire app) */
    UartReceiver* uart_receiver;
    ButtonEngine* engine;
    FuriTimer* engine_timer;      /* 10ms periodic tick */

    /* Controller state (shared across scenes, updated by UART worker + timer) */
    bool active;
    bool paused;
    bool wifi_connected;
    uint32_t action_counter;
    uint32_t start_tick;
    uint32_t last_heartbeat_tick;
    uint8_t selected_mode;
    bool usb_started;
    uint16_t display_update_counter; /* Ticked at 100Hz, display updates at ~1Hz */
} SwitchControllerApp;
