/**
 * Controller scene: Live status display with mode-specific behavior.
 *
 * Manual Mode: Flipper buttons map directly to Switch Pro Controller buttons.
 *   - D-pad: D-pad
 *   - OK: A button
 *   - Back: B button (long press for exit)
 *
 * Remote Mode: Displays status of WiFi bridge connection and received commands.
 *   - OK: Toggle pause/resume
 *   - Back: Confirm exit dialog
 *
 * Timer updates the display model at ~1Hz with elapsed time and status.
 */

#include "../switch_controller_i.h"

#define TAG "SceneController"

/* ---------- Scene Handlers ---------- */

void scene_controller_on_enter(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    /* Initialize state */
    app->active = true;
    app->paused = false;
    app->action_counter = 0;
    app->start_tick = furi_get_tick();

    /* Determine mode text */
    const char* mode_text;
    switch(app->selected_mode) {
    case ControllerModeManual:
        mode_text = "Manual (Flipper)";
        break;
    case ControllerModeRemote:
        mode_text = "Remote (WiFi)";
        break;
    default:
        mode_text = "Unknown";
        break;
    }

    /* Update the controller view model */
    with_view_model(
        app->controller_view,
        ControllerModel * m,
        {
            m->active = true;
            m->paused = false;
            m->wifi_connected = app->wifi_connected;
            m->action_count = 0;
            m->elapsed_seconds = 0;
            m->manual_mode = (app->selected_mode == ControllerModeManual);
            strncpy(m->mode_text, mode_text, sizeof(m->mode_text) - 1);
            m->mode_text[sizeof(m->mode_text) - 1] = '\0';
            strncpy(m->state_text, "Ready", sizeof(m->state_text) - 1);
            m->state_text[sizeof(m->state_text) - 1] = '\0';
        },
        true);

    /* Switch to the controller view */
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewController);

    FURI_LOG_I(TAG, "Controller started: mode=%s", mode_text);
}

bool scene_controller_on_event(void* context, SceneManagerEvent event) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    if(event.type == SceneManagerEventTypeBack) {
        /* Back button pressed: pause if running, then show confirm exit */
        if(app->active && !app->paused) {
            /* Pause first: release all buttons and notify PC */
            app->paused = true;
            button_engine_release(app->engine, "ALL");
            uart_receiver_send(app->uart_receiver, "C:PAUSE\n");

            /* Update view model to show paused state */
            with_view_model(
                app->controller_view,
                ControllerModel * m,
                {
                    m->paused = true;
                    strncpy(m->state_text, "Paused", sizeof(m->state_text) - 1);
                    m->state_text[sizeof(m->state_text) - 1] = '\0';
                },
                true);
        }

        /* Navigate to confirm exit dialog */
        scene_manager_next_scene(app->scene_manager, SceneConfirmExit);
        return true;
    }

    return false;
}

void scene_controller_on_exit(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    /* Reset state */
    app->active = false;
    app->paused = false;

    /* Reset the view model to defaults */
    with_view_model(
        app->controller_view,
        ControllerModel * m,
        {
            m->active = false;
            m->paused = false;
            m->wifi_connected = false;
            m->action_count = 0;
            m->elapsed_seconds = 0;
            strncpy(m->state_text, "Idle", sizeof(m->state_text) - 1);
            m->state_text[sizeof(m->state_text) - 1] = '\0';
            m->mode_text[0] = '\0';
        },
        true);

    FURI_LOG_I(TAG, "Controller scene exited");
}
