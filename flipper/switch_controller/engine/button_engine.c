/**
 * Button engine implementation.
 *
 * Manages timed button presses with auto-release and macro sequence
 * execution. Maintains a SwitchButtonState that is pushed to the USB
 * layer after each state change.
 *
 * Supports up to 8 simultaneous timed actions (buttons or sticks).
 */

#include "button_engine.h"
#include "sequences.h"
#include "../usb/switch_pro_usb.h"
#include "../usb/switch_pro_report.h"
#include <furi.h>
#include <string.h>

#define TAG "ButtonEngine"

#define MAX_TIMED_ACTIONS 8

/* ---------- Timed Action ---------- */

typedef enum {
    TimedActionButton,
    TimedActionStick,
} TimedActionType;

typedef struct {
    bool active;
    TimedActionType type;
    uint32_t expiry_tick;   /* furi_get_tick() when this action expires */

    /* For button actions: which buttons were set */
    bool btns_a, btns_b, btns_x, btns_y;
    bool btns_l, btns_r, btns_zl, btns_zr;
    bool btns_up, btns_down, btns_left, btns_right;
    bool btns_plus, btns_minus, btns_home;
    bool btns_lstick, btns_rstick;

    /* For stick actions */
    bool is_left_stick; /* true = left, false = right */
} TimedAction;

/* ---------- Macro State ---------- */

typedef struct {
    const MacroSequence* sequence;
    size_t current_step;
    uint32_t step_start_tick;
    bool in_pause;        /* true = between steps (pause_ms) */
    bool buttons_pressed; /* true = buttons are held for current step */
} MacroState;

/* ---------- Button Engine ---------- */

struct ButtonEngine {
    SwitchButtonState current_state;
    TimedAction actions[MAX_TIMED_ACTIONS];
    MacroState macro;
    bool state_dirty; /* true if state changed since last USB push */
    char status_buf[32];
};

/* ---------- Button String Parsing Helpers ---------- */

/**
 * Apply a button string to a SwitchButtonState, setting matching
 * buttons to the given value.
 *
 * For multi-character button names (UP, DOWN, LEFT, RIGHT, ZL, ZR,
 * PLUS, MINUS, HOME, LSTICK, RSTICK), the entire string must match.
 * For single-character names (A, B, X, Y, L, R), each character in
 * the string is applied independently (so "ABXY" sets A, B, X, Y).
 */
static void apply_buttons_to_state(
    SwitchButtonState* state,
    const char* buttons_str,
    bool value) {
    if(buttons_str == NULL || buttons_str[0] == '\0') return;

    /* Try multi-character button names first */
    if(strcmp(buttons_str, "UP") == 0) {
        state->up = value;
        return;
    }
    if(strcmp(buttons_str, "DOWN") == 0) {
        state->down = value;
        return;
    }
    if(strcmp(buttons_str, "LEFT") == 0) {
        state->left = value;
        return;
    }
    if(strcmp(buttons_str, "RIGHT") == 0) {
        state->right = value;
        return;
    }
    if(strcmp(buttons_str, "ZL") == 0) {
        state->zl = value;
        return;
    }
    if(strcmp(buttons_str, "ZR") == 0) {
        state->zr = value;
        return;
    }
    if(strcmp(buttons_str, "PLUS") == 0 || strcmp(buttons_str, "+") == 0) {
        state->plus = value;
        return;
    }
    if(strcmp(buttons_str, "MINUS") == 0 || strcmp(buttons_str, "-") == 0) {
        state->minus = value;
        return;
    }
    if(strcmp(buttons_str, "HOME") == 0) {
        state->home = value;
        return;
    }
    if(strcmp(buttons_str, "LSTICK") == 0) {
        state->lstick = value;
        return;
    }
    if(strcmp(buttons_str, "RSTICK") == 0) {
        state->rstick = value;
        return;
    }

    /* Single-character button names: iterate each char */
    for(const char* p = buttons_str; *p != '\0'; p++) {
        switch(*p) {
        case 'A':
            state->a = value;
            break;
        case 'B':
            state->b = value;
            break;
        case 'X':
            state->x = value;
            break;
        case 'Y':
            state->y = value;
            break;
        case 'L':
            state->l = value;
            break;
        case 'R':
            state->r = value;
            break;
        default:
            FURI_LOG_W(TAG, "Unknown button char: '%c'", *p);
            break;
        }
    }
}

/**
 * Record which buttons a timed action is responsible for, so we know
 * which to clear on expiry.
 */
static void record_buttons_in_action(TimedAction* action, const char* buttons_str) {
    if(buttons_str == NULL) return;

    /* Multi-character names */
    if(strcmp(buttons_str, "UP") == 0) {
        action->btns_up = true;
        return;
    }
    if(strcmp(buttons_str, "DOWN") == 0) {
        action->btns_down = true;
        return;
    }
    if(strcmp(buttons_str, "LEFT") == 0) {
        action->btns_left = true;
        return;
    }
    if(strcmp(buttons_str, "RIGHT") == 0) {
        action->btns_right = true;
        return;
    }
    if(strcmp(buttons_str, "ZL") == 0) {
        action->btns_zl = true;
        return;
    }
    if(strcmp(buttons_str, "ZR") == 0) {
        action->btns_zr = true;
        return;
    }
    if(strcmp(buttons_str, "PLUS") == 0 || strcmp(buttons_str, "+") == 0) {
        action->btns_plus = true;
        return;
    }
    if(strcmp(buttons_str, "MINUS") == 0 || strcmp(buttons_str, "-") == 0) {
        action->btns_minus = true;
        return;
    }
    if(strcmp(buttons_str, "HOME") == 0) {
        action->btns_home = true;
        return;
    }
    if(strcmp(buttons_str, "LSTICK") == 0) {
        action->btns_lstick = true;
        return;
    }
    if(strcmp(buttons_str, "RSTICK") == 0) {
        action->btns_rstick = true;
        return;
    }

    /* Single chars */
    for(const char* p = buttons_str; *p != '\0'; p++) {
        switch(*p) {
        case 'A':
            action->btns_a = true;
            break;
        case 'B':
            action->btns_b = true;
            break;
        case 'X':
            action->btns_x = true;
            break;
        case 'Y':
            action->btns_y = true;
            break;
        case 'L':
            action->btns_l = true;
            break;
        case 'R':
            action->btns_r = true;
            break;
        default:
            break;
        }
    }
}

/**
 * Clear buttons owned by a timed action from the current state.
 */
static void clear_action_buttons(SwitchButtonState* state, const TimedAction* action) {
    if(action->btns_a) state->a = false;
    if(action->btns_b) state->b = false;
    if(action->btns_x) state->x = false;
    if(action->btns_y) state->y = false;
    if(action->btns_l) state->l = false;
    if(action->btns_r) state->r = false;
    if(action->btns_zl) state->zl = false;
    if(action->btns_zr) state->zr = false;
    if(action->btns_up) state->up = false;
    if(action->btns_down) state->down = false;
    if(action->btns_left) state->left = false;
    if(action->btns_right) state->right = false;
    if(action->btns_plus) state->plus = false;
    if(action->btns_minus) state->minus = false;
    if(action->btns_home) state->home = false;
    if(action->btns_lstick) state->lstick = false;
    if(action->btns_rstick) state->rstick = false;
}

/**
 * Find a free timed action slot, or NULL if all are in use.
 */
static TimedAction* find_free_action(ButtonEngine* engine) {
    for(size_t i = 0; i < MAX_TIMED_ACTIONS; i++) {
        if(!engine->actions[i].active) {
            return &engine->actions[i];
        }
    }
    return NULL;
}

/* ---------- Public API ---------- */

ButtonEngine* button_engine_alloc(void) {
    ButtonEngine* engine = malloc(sizeof(ButtonEngine));
    memset(engine, 0, sizeof(ButtonEngine));

    switch_pro_report_init(&engine->current_state);

    engine->macro.sequence = NULL;
    engine->state_dirty = false;
    snprintf(engine->status_buf, sizeof(engine->status_buf), "Idle");

    return engine;
}

void button_engine_free(ButtonEngine* engine) {
    if(engine == NULL) return;

    /* Release all buttons before freeing */
    switch_pro_report_init(&engine->current_state);
    switch_pro_usb_set_button_state(&engine->current_state);

    free(engine);
}

void button_engine_press(ButtonEngine* engine, const char* buttons_str, uint32_t duration_ms) {
    furi_check(engine != NULL);

    TimedAction* action = find_free_action(engine);
    if(action == NULL) {
        FURI_LOG_W(TAG, "No free timed action slots");
        return;
    }

    memset(action, 0, sizeof(TimedAction));
    action->active = true;
    action->type = TimedActionButton;
    action->expiry_tick = furi_get_tick() + furi_ms_to_ticks(duration_ms);

    /* Record which buttons this action owns */
    record_buttons_in_action(action, buttons_str);

    /* Set buttons in current state */
    apply_buttons_to_state(&engine->current_state, buttons_str, true);
    engine->state_dirty = true;

    FURI_LOG_D(TAG, "Press: %s for %lums", buttons_str, (unsigned long)duration_ms);
}

void button_engine_release(ButtonEngine* engine, const char* buttons_str) {
    furi_check(engine != NULL);

    if(strcmp(buttons_str, "ALL") == 0) {
        /* Release everything: clear all buttons, center sticks, cancel actions */
        switch_pro_report_init(&engine->current_state);
        for(size_t i = 0; i < MAX_TIMED_ACTIONS; i++) {
            engine->actions[i].active = false;
        }
        engine->macro.sequence = NULL;
        FURI_LOG_D(TAG, "Release ALL");
    } else {
        /* Release specific buttons */
        apply_buttons_to_state(&engine->current_state, buttons_str, false);

        /* Cancel any timed actions for these buttons */
        /* (simplified: just let them expire naturally; the buttons are already released) */
        FURI_LOG_D(TAG, "Release: %s", buttons_str);
    }

    engine->state_dirty = true;
}

void button_engine_stick(
    ButtonEngine* engine,
    const char* stick,
    int16_t x,
    int16_t y,
    uint32_t duration_ms) {
    furi_check(engine != NULL);

    bool is_left = (stick[0] == 'L');

    TimedAction* action = find_free_action(engine);
    if(action == NULL) {
        FURI_LOG_W(TAG, "No free timed action slots for stick");
        return;
    }

    memset(action, 0, sizeof(TimedAction));
    action->active = true;
    action->type = TimedActionStick;
    action->is_left_stick = is_left;
    action->expiry_tick = furi_get_tick() + furi_ms_to_ticks(duration_ms);

    /* Set stick position */
    if(is_left) {
        engine->current_state.left_x = (uint16_t)x;
        engine->current_state.left_y = (uint16_t)y;
    } else {
        engine->current_state.right_x = (uint16_t)x;
        engine->current_state.right_y = (uint16_t)y;
    }

    engine->state_dirty = true;

    FURI_LOG_D(
        TAG,
        "Stick %s: x=%d y=%d for %lums",
        stick,
        x,
        y,
        (unsigned long)duration_ms);
}

bool button_engine_macro(ButtonEngine* engine, const char* macro_name) {
    furi_check(engine != NULL);

    if(engine->macro.sequence != NULL) {
        FURI_LOG_W(TAG, "Macro already running, rejecting %s", macro_name);
        return false;
    }

    const MacroSequence* seq = find_macro(macro_name);
    if(seq == NULL) {
        FURI_LOG_W(TAG, "Unknown macro: %s", macro_name);
        return false;
    }

    engine->macro.sequence = seq;
    engine->macro.current_step = 0;
    engine->macro.step_start_tick = furi_get_tick();
    engine->macro.in_pause = false;
    engine->macro.buttons_pressed = false;

    FURI_LOG_I(TAG, "Macro started: %s (%zu steps)", macro_name, seq->step_count);
    return true;
}

void button_engine_tick(ButtonEngine* engine) {
    if(engine == NULL) return;

    uint32_t now = furi_get_tick();

    /* --- Process timed button/stick actions --- */
    for(size_t i = 0; i < MAX_TIMED_ACTIONS; i++) {
        TimedAction* action = &engine->actions[i];
        if(!action->active) continue;

        if(now >= action->expiry_tick) {
            /* Action expired -- release */
            if(action->type == TimedActionButton) {
                clear_action_buttons(&engine->current_state, action);
            } else if(action->type == TimedActionStick) {
                /* Return stick to center */
                if(action->is_left_stick) {
                    engine->current_state.left_x = 0x800;
                    engine->current_state.left_y = 0x800;
                } else {
                    engine->current_state.right_x = 0x800;
                    engine->current_state.right_y = 0x800;
                }
            }
            action->active = false;
            engine->state_dirty = true;
        }
    }

    /* --- Process macro execution --- */
    if(engine->macro.sequence != NULL) {
        const MacroSequence* seq = engine->macro.sequence;
        size_t step_idx = engine->macro.current_step;

        if(step_idx >= seq->step_count) {
            /* Macro complete */
            engine->macro.sequence = NULL;
            FURI_LOG_I(TAG, "Macro complete");
        } else {
            const SequenceStep* step = &seq->steps[step_idx];
            uint32_t elapsed = now - engine->macro.step_start_tick;

            if(engine->macro.in_pause) {
                /* Waiting between steps */
                if(elapsed >= furi_ms_to_ticks(step->pause_ms)) {
                    /* Pause done, advance to next step */
                    engine->macro.current_step++;
                    engine->macro.step_start_tick = now;
                    engine->macro.in_pause = false;
                    engine->macro.buttons_pressed = false;
                }
            } else if(!engine->macro.buttons_pressed) {
                /* Press buttons for this step */
                apply_buttons_to_state(&engine->current_state, step->buttons, true);
                engine->macro.buttons_pressed = true;
                engine->macro.step_start_tick = now;
                engine->state_dirty = true;
            } else {
                /* Buttons held -- check if duration elapsed */
                if(elapsed >= furi_ms_to_ticks(step->duration_ms)) {
                    /* Release buttons */
                    apply_buttons_to_state(&engine->current_state, step->buttons, false);
                    engine->state_dirty = true;

                    if(step->pause_ms > 0) {
                        /* Enter pause phase */
                        engine->macro.in_pause = true;
                        engine->macro.step_start_tick = now;
                    } else {
                        /* No pause, advance immediately */
                        engine->macro.current_step++;
                        engine->macro.step_start_tick = now;
                        engine->macro.in_pause = false;
                        engine->macro.buttons_pressed = false;
                    }
                }
            }
        }
    }

    /* --- Push state to USB if changed --- */
    if(engine->state_dirty) {
        switch_pro_usb_set_button_state(&engine->current_state);
        engine->state_dirty = false;
    }

    /* --- Update status string --- */
    if(engine->macro.sequence != NULL) {
        snprintf(
            engine->status_buf,
            sizeof(engine->status_buf),
            "Macro: %s",
            engine->macro.sequence->name);
    } else {
        bool any_active = false;
        for(size_t i = 0; i < MAX_TIMED_ACTIONS; i++) {
            if(engine->actions[i].active) {
                any_active = true;
                break;
            }
        }
        if(any_active) {
            snprintf(engine->status_buf, sizeof(engine->status_buf), "Pressing...");
        } else {
            snprintf(engine->status_buf, sizeof(engine->status_buf), "Idle");
        }
    }
}

bool button_engine_is_busy(ButtonEngine* engine) {
    if(engine == NULL) return false;

    if(engine->macro.sequence != NULL) return true;

    for(size_t i = 0; i < MAX_TIMED_ACTIONS; i++) {
        if(engine->actions[i].active) return true;
    }

    return false;
}

const char* button_engine_get_status(ButtonEngine* engine) {
    if(engine == NULL) return "N/A";
    return engine->status_buf;
}
