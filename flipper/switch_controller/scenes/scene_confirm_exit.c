/**
 * Confirm exit scene: Dialog asking whether to stop the controller.
 *
 * Shown when the user presses Back during active control. Uses a Widget
 * module with "Stop controller and exit?" text and Yes/No buttons.
 *
 * Navigation:
 *   Yes (left button) -> Release all buttons, send C:STOP, return to menu
 *   No (right button) -> Return to controller scene (resumes if was running)
 *   Back -> Same as No (cancel exit, return to controller)
 */

#include "../switch_controller_i.h"

#define TAG "SceneConfirmExit"

/* Custom event IDs for dialog buttons */
enum {
    ConfirmExitEventConfirm,
    ConfirmExitEventCancel,
};

/* ---------- Widget Button Callbacks ---------- */

static void confirm_exit_yes_callback(GuiButtonType result, InputType type, void* context) {
    UNUSED(result);
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfirmExitEventConfirm);
    }
}

static void confirm_exit_no_callback(GuiButtonType result, InputType type, void* context) {
    UNUSED(result);
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfirmExitEventCancel);
    }
}

/* ---------- Scene Handlers ---------- */

void scene_confirm_exit_on_enter(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    /* Reset and configure widget */
    widget_reset(app->confirm_exit_widget);

    /* Add dialog text */
    widget_add_string_element(
        app->confirm_exit_widget, 64, 20, AlignCenter, AlignCenter,
        FontPrimary, "Stop controller?");

    widget_add_string_element(
        app->confirm_exit_widget, 64, 35, AlignCenter, AlignCenter,
        FontSecondary, "Will release all buttons.");

    /* Add Yes/No buttons */
    widget_add_button_element(
        app->confirm_exit_widget, GuiButtonTypeLeft, "Yes",
        confirm_exit_yes_callback, app);

    widget_add_button_element(
        app->confirm_exit_widget, GuiButtonTypeRight, "No",
        confirm_exit_no_callback, app);

    /* Switch to confirm exit view */
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewConfirmExit);

    FURI_LOG_I(TAG, "Confirm exit dialog shown");
}

bool scene_confirm_exit_on_event(void* context, SceneManagerEvent event) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ConfirmExitEventConfirm) {
            /* Yes: stop, release buttons, notify PC, go to menu */
            button_engine_release(app->engine, "ALL");
            uart_receiver_send(app->uart_receiver, "C:STOP\n");
            app->active = false;
            app->paused = false;

            FURI_LOG_I(TAG, "Controller stopped by user, returning to menu");

            /* Go back to menu (clears scene stack) */
            scene_manager_search_and_switch_to_another_scene(
                app->scene_manager, SceneMenu);
            return true;
        }

        if(event.event == ConfirmExitEventCancel) {
            /* No: go back to controller scene (resume) */
            FURI_LOG_I(TAG, "Exit cancelled, returning to controller");
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        /* Back button: treat as cancel (same as No) */
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }

    return false;
}

void scene_confirm_exit_on_exit(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    widget_reset(app->confirm_exit_widget);
}
