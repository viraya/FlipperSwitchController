/**
 * Menu scene: Mode selection for the Switch Controller.
 *
 * Options:
 *   - Manual Mode: Control Switch directly with Flipper buttons (no ESP32)
 *   - Remote Mode: WiFi remote control via ESP32 bridge
 *   - USB Debug: View USB handshake diagnostics
 */

#include "../switch_controller_i.h"

#define TAG "SceneMenu"

#define MENU_USB_DEBUG 99

/* ---------- Submenu Callback ---------- */

static void menu_item_callback(void* context, uint32_t index) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    if(index == MENU_USB_DEBUG) {
        view_dispatcher_send_custom_event(app->view_dispatcher, MENU_USB_DEBUG);
    } else {
        app->selected_mode = (uint8_t)index;
        view_dispatcher_send_custom_event(app->view_dispatcher, index);
    }
}

/* ---------- Scene Handlers ---------- */

void scene_menu_on_enter(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Switch Controller");

    /* Controller mode options */
    submenu_add_item(
        app->submenu, "Manual Mode (Flipper)", ControllerModeManual,
        menu_item_callback, app);
    submenu_add_item(
        app->submenu, "Remote Mode (WiFi)", ControllerModeRemote,
        menu_item_callback, app);

    /* USB Debug page */
    submenu_add_item(
        app->submenu,
        app->usb_started ? "USB Debug (connected)" : "USB Debug (connect)",
        MENU_USB_DEBUG,
        menu_item_callback, app);

    /* Restore previous selection */
    uint32_t state = scene_manager_get_scene_state(app->scene_manager, SceneMenu);
    submenu_set_selected_item(app->submenu, state);

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
}

bool scene_menu_on_event(void* context, SceneManagerEvent event) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MENU_USB_DEBUG) {
            scene_manager_next_scene(app->scene_manager, SceneUsbDebug);
            return true;
        }

        if(event.event == 100) {
            /* Auto-start from PC: transition to controller scene */
            scene_manager_next_scene(app->scene_manager, SceneController);
            return true;
        }

        /* Mode selected */
        scene_manager_set_scene_state(
            app->scene_manager, SceneMenu, event.event);
        scene_manager_next_scene(app->scene_manager, SceneController);
        return true;
    }

    return false;
}

void scene_menu_on_exit(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    submenu_reset(app->submenu);
}
