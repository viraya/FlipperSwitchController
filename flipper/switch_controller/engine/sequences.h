#pragma once

/**
 * Predefined macro sequences for the button engine.
 *
 * Each macro is a series of steps: press button(s) for a duration,
 * optionally pause, then move to the next step.
 */

#include <stdint.h>
#include <stddef.h>

/* A single step in a macro sequence */
typedef struct {
    const char* buttons;    /* Button string (e.g., "ABXY", "A") */
    uint32_t duration_ms;   /* How long to hold the buttons */
    uint32_t pause_ms;      /* Pause after releasing before next step */
} SequenceStep;

/* A complete macro sequence */
typedef struct {
    const char* name;
    const SequenceStep* steps;
    size_t step_count;
} MacroSequence;

/**
 * Look up a macro sequence by name (case-sensitive).
 * @param name  Macro name (e.g., "SOFT_RESET", "PRESS_A").
 * @return Pointer to the macro, or NULL if not found.
 */
const MacroSequence* find_macro(const char* name);
