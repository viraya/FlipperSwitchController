/**
 * Predefined macro sequence data.
 *
 * Sequences are stored as static const arrays. The find_macro()
 * function performs a linear search by name.
 */

#include "sequences.h"
#include <string.h>

/* ---------- SOFT_RESET: A+B+X+Y for 200ms ---------- */

static const SequenceStep sequence_soft_reset[] = {
    {.buttons = "ABXY", .duration_ms = 200, .pause_ms = 0},
};

/* ---------- PRESS_A: A for 100ms ---------- */

static const SequenceStep sequence_press_a[] = {
    {.buttons = "A", .duration_ms = 100, .pause_ms = 0},
};

/* ---------- PRESS_B: B for 100ms ---------- */

static const SequenceStep sequence_press_b[] = {
    {.buttons = "B", .duration_ms = 100, .pause_ms = 0},
};

/* ---------- RUN_AWAY: Navigate 2x2 menu to bottom-right option ---------- */
/* Common 2x2 battle menu layout: TL | TR / BL | BR
 * Cursor starts at top-left. Navigate DOWN, RIGHT, then A to confirm. */

static const SequenceStep sequence_run_away[] = {
    {.buttons = "DOWN",  .duration_ms = 80, .pause_ms = 150},
    {.buttons = "RIGHT", .duration_ms = 80, .pause_ms = 150},
    {.buttons = "A",     .duration_ms = 100, .pause_ms = 0},
};

/* ---------- Macro Registry ---------- */

static const MacroSequence macros[] = {
    {
        .name = "SOFT_RESET",
        .steps = sequence_soft_reset,
        .step_count = sizeof(sequence_soft_reset) / sizeof(sequence_soft_reset[0]),
    },
    {
        .name = "PRESS_A",
        .steps = sequence_press_a,
        .step_count = sizeof(sequence_press_a) / sizeof(sequence_press_a[0]),
    },
    {
        .name = "PRESS_B",
        .steps = sequence_press_b,
        .step_count = sizeof(sequence_press_b) / sizeof(sequence_press_b[0]),
    },
    {
        .name = "RUN_AWAY",
        .steps = sequence_run_away,
        .step_count = sizeof(sequence_run_away) / sizeof(sequence_run_away[0]),
    },
};

static const size_t macro_count = sizeof(macros) / sizeof(macros[0]);

const MacroSequence* find_macro(const char* name) {
    if(name == NULL) return NULL;

    for(size_t i = 0; i < macro_count; i++) {
        if(strcmp(macros[i].name, name) == 0) {
            return &macros[i];
        }
    }

    return NULL;
}
