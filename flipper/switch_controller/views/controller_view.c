/**
 * Custom view implementation for the controller status display.
 *
 * Layout for 128x64 monochrome LCD:
 *
 * +----------------------------------+  (0,0)
 * | SwitchCtrl      WiFi:OK USB:OK  |  Row 0-14: Title + status
 * |----------------------------------|  (0,15) separator line
 * | State: Ready                     |  Row 16-27: Current state
 * | Mode: Manual (Flipper)           |  Row 28-38: Controller mode
 * | Actions: 42         0:05:30     |  Row 39-50: Counter + timer
 * |----------------------------------|  (0,52) separator line
 * | [OK] Pause   [<] Exit           |  Row 53-63: Controls hint
 * +----------------------------------+  (127,63)
 *
 * Manual mode input mapping:
 *   Flipper D-pad  -> Switch D-pad
 *   Flipper OK     -> Switch A button
 *   Flipper Back   -> Switch B button (short press) / Exit (long press)
 */

#include "controller_view.h"
#include "../switch_controller_i.h"
#include <gui/canvas.h>
#include <stdio.h>
#include <string.h>

/* ---------- Draw Callback ---------- */

static void controller_draw(Canvas* canvas, void* model) {
    ControllerModel* m = (ControllerModel*)model;

    canvas_clear(canvas);

    /* Title bar */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "SwitchCtrl");

    /* WiFi + USB status at top-right */
    canvas_set_font(canvas, FontSecondary);
    if(m->wifi_connected && m->usb_connected) {
        canvas_draw_str(canvas, 68, 7, "WiFi:OK USB:OK");
    } else if(m->wifi_connected) {
        canvas_draw_str(canvas, 68, 7, "WiFi:OK USB:--");
    } else if(m->usb_connected) {
        canvas_draw_str(canvas, 68, 7, "WiFi:-- USB:OK");
    } else {
        canvas_draw_str(canvas, 68, 7, "WiFi:-- USB:--");
    }

    /* Separator line below title */
    canvas_draw_line(canvas, 0, 15, 128, 15);

    /* State text */
    char buf[40];
    snprintf(buf, sizeof(buf), "State: %s", m->state_text);
    canvas_draw_str(canvas, 2, 27, buf);

    /* Mode name */
    snprintf(buf, sizeof(buf), "Mode: %s", m->mode_text);
    canvas_draw_str(canvas, 2, 38, buf);

    /* Action counter */
    snprintf(buf, sizeof(buf), "Actions: %lu", (unsigned long)m->action_count);
    canvas_draw_str(canvas, 2, 50, buf);

    /* Elapsed time as H:MM:SS at right side */
    uint32_t h = m->elapsed_seconds / 3600;
    uint32_t mins = (m->elapsed_seconds % 3600) / 60;
    uint32_t s = m->elapsed_seconds % 60;
    snprintf(
        buf,
        sizeof(buf),
        "%lu:%02lu:%02lu",
        (unsigned long)h,
        (unsigned long)mins,
        (unsigned long)s);
    canvas_draw_str(canvas, 80, 50, buf);

    /* Separator line above controls hint */
    canvas_draw_line(canvas, 0, 52, 128, 52);

    /* Controls hint at bottom */
    if(!m->active) {
        canvas_draw_str(canvas, 2, 63, "Waiting for connection...");
    } else if(m->manual_mode) {
        canvas_draw_str(canvas, 2, 63, "D-pad+OK=ctrl  < Exit");
    } else if(m->paused) {
        canvas_draw_str(canvas, 2, 63, "OK Resume  < Exit");
    } else {
        canvas_draw_str(canvas, 2, 63, "OK Pause   < Exit");
    }
}

/* ---------- Input Callback ---------- */

static bool controller_input_callback(InputEvent* event, void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;
    bool consumed = false;

    /* Manual mode: Flipper buttons -> Switch Pro Controller buttons */
    if(app->selected_mode == ControllerModeManual) {
        SwitchButtonState state;
        switch_pro_report_init(&state);

        if(event->type == InputTypePress || event->type == InputTypeLong ||
           event->type == InputTypeRepeat) {
            switch(event->key) {
            case InputKeyUp:
                state.up = true;
                switch_pro_usb_set_button_state(&state);
                app->action_counter++;
                with_view_model(
                    app->controller_view,
                    ControllerModel * m,
                    {
                        strncpy(m->state_text, "D-pad Up", sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    },
                    true);
                consumed = true;
                break;
            case InputKeyDown:
                state.down = true;
                switch_pro_usb_set_button_state(&state);
                app->action_counter++;
                with_view_model(
                    app->controller_view,
                    ControllerModel * m,
                    {
                        strncpy(m->state_text, "D-pad Down", sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    },
                    true);
                consumed = true;
                break;
            case InputKeyLeft:
                state.left = true;
                switch_pro_usb_set_button_state(&state);
                app->action_counter++;
                with_view_model(
                    app->controller_view,
                    ControllerModel * m,
                    {
                        strncpy(m->state_text, "D-pad Left", sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    },
                    true);
                consumed = true;
                break;
            case InputKeyRight:
                state.right = true;
                switch_pro_usb_set_button_state(&state);
                app->action_counter++;
                with_view_model(
                    app->controller_view,
                    ControllerModel * m,
                    {
                        strncpy(m->state_text, "D-pad Right", sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    },
                    true);
                consumed = true;
                break;
            case InputKeyOk:
                state.a = true;
                switch_pro_usb_set_button_state(&state);
                app->action_counter++;
                with_view_model(
                    app->controller_view,
                    ControllerModel * m,
                    {
                        strncpy(m->state_text, "Button A", sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    },
                    true);
                consumed = true;
                break;
            case InputKeyBack:
                if(event->type == InputTypeLong) {
                    /* Long press back = exit (handled by scene manager) */
                    consumed = false;
                } else {
                    /* Short press back = Switch B button */
                    state.b = true;
                    switch_pro_usb_set_button_state(&state);
                    app->action_counter++;
                    with_view_model(
                        app->controller_view,
                        ControllerModel * m,
                        {
                            strncpy(m->state_text, "Button B", sizeof(m->state_text) - 1);
                            m->state_text[sizeof(m->state_text) - 1] = '\0';
                        },
                        true);
                    consumed = true;
                }
                break;
            default:
                break;
            }
        } else if(event->type == InputTypeRelease) {
            /* Release all buttons */
            switch_pro_usb_set_button_state(&state);
            if(event->key == InputKeyBack) return false;
            with_view_model(
                app->controller_view,
                ControllerModel * m,
                {
                    strncpy(m->state_text, "Ready", sizeof(m->state_text) - 1);
                    m->state_text[sizeof(m->state_text) - 1] = '\0';
                },
                true);
            consumed = true;
        }
        return consumed;
    }

    /* Remote mode: OK = pause/resume */
    if(event->type == InputTypePress && event->key == InputKeyOk) {
        if(app->active) {
            app->paused = !app->paused;

            if(app->paused) {
                button_engine_release(app->engine, "ALL");
                uart_receiver_send(app->uart_receiver, "C:PAUSE\n");
            } else {
                uart_receiver_send(app->uart_receiver, "C:RESUME\n");
            }

            with_view_model(
                app->controller_view,
                ControllerModel * m,
                {
                    m->paused = app->paused;
                    if(app->paused) {
                        strncpy(m->state_text, "Paused", sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    } else {
                        strncpy(m->state_text, "Resuming", sizeof(m->state_text) - 1);
                        m->state_text[sizeof(m->state_text) - 1] = '\0';
                    }
                },
                true);
        }
        consumed = true;
    }

    return consumed;
}

/* ---------- Public API ---------- */

View* controller_view_alloc(void* app_context) {
    View* view = view_alloc();

    view_allocate_model(view, ViewModelTypeLockFree, sizeof(ControllerModel));
    view_set_draw_callback(view, controller_draw);
    view_set_context(view, app_context);
    view_set_input_callback(view, controller_input_callback);

    with_view_model(
        view,
        ControllerModel * m,
        {
            m->active = false;
            m->paused = false;
            m->wifi_connected = false;
            m->usb_connected = false;
            m->manual_mode = false;
            m->action_count = 0;
            m->elapsed_seconds = 0;
            strncpy(m->state_text, "Waiting", sizeof(m->state_text) - 1);
            m->state_text[sizeof(m->state_text) - 1] = '\0';
            m->mode_text[0] = '\0';
        },
        true);

    return view;
}

void controller_view_free(View* view) {
    if(view) {
        view_free(view);
    }
}
