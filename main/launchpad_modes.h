/*
 * launchpad_modes.h — Launchpad screen's pad-to-MIDI-message mapping.
 *
 * Pure mapping logic, no touch/display/MIDI-transport dependency of any
 * kind (not even SGFX) -- a pad index and a press/release edge go in, a
 * MIDI message (or nothing) comes out. main/launchpad_app.c is the only
 * place that actually calls orion_usb_midi_send(); this file just decides
 * *what* to send for a given mode.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LP_MODE_NOTE = 0,   /* isomorphic chromatic grid */
    LP_MODE_DRUM,       /* fixed GM percussion-map pads */
    LP_MODE_MIXER_CC,   /* one continuous-drag CC "fader" per column */
    LP_MODE_XY_MACRO,   /* whole area as one continuous 2D touch surface */
    LP_MODE_KNOB,       /* grid of relative-drag rotary CC knobs */
    LP_MODE_LOOPER,     /* record/play/overdub whatever the other modes send */
    LP_MODE_COUNT
} lp_mode_t;

typedef struct { int rows, cols; } lp_grid_shape_t;

#define LP_MAX_VARIANTS 3

/* How many grid-size options a mode offers. NOTE and MIXER-CC are
 * formulaic (their note/CC numbering is computed from row/col, so any
 * rows x cols works) and offer 3 each. DRUM is fixed at 1 -- its note
 * mapping is a real GM percussion lookup table keyed by a specific 4x4
 * layout, not a formula, so a different shape would need a different
 * table, not just different math (not done -- scope decision, not an
 * oversight). XY-macro is fixed at 1 -- it has no discrete grid at all. */
int lp_mode_variant_count(lp_mode_t mode);

/* Discrete-pad grid shape for a mode at a given variant index (0 ..
 * lp_mode_variant_count(mode)-1; out-of-range clamps to 0). LP_MODE_XY_MACRO
 * returns {1,1} regardless of variant: it has no discrete pads, the whole
 * area is one continuous surface -- see lp_mode_xy_event() instead. */
lp_grid_shape_t lp_mode_grid_shape(lp_mode_t mode, int variant);

/* Short label for the mode-row tab (<=6 chars, fits the tab width at every
 * UI scale this screen supports). */
const char* lp_mode_label(lp_mode_t mode);

typedef enum {
    LP_MSG_NOTE_ON = 0,
    LP_MSG_NOTE_OFF,
    LP_MSG_CC,
} lp_msg_kind_t;

/* channel is intentionally NOT a field here -- it's applied once, by
 * whatever builds the raw 3-byte USB-MIDI packet (launchpad_app.c), so
 * this struct stays a pure "what to play" value regardless of which
 * channel it ends up on. */
typedef struct {
    lp_msg_kind_t kind;
    uint8_t        number;  /* note number (0-127) or CC number (0-127) */
    uint8_t        value;   /* velocity (Note On) or CC value, 0-127; unused (0) for Note Off */
} lp_midi_msg_t;

typedef struct {
    int           valid;   /* 0 if this pad/edge combination emits nothing */
    lp_midi_msg_t msg;
} lp_msg_result_t;

/* pad_index is row-major (row * cols + col) within lp_mode_grid_shape()'s
 * shape AT THIS SAME variant. pressed is 1 on touch-down, 0 on touch-up/
 * release. Not valid for LP_MODE_XY_MACRO (use lp_mode_xy_event() instead)
 * -- always returns .valid = 0 for that mode. */
lp_msg_result_t lp_mode_pad_event(lp_mode_t mode, int variant, int pad_index, int pressed);

/* LP_MODE_XY_MACRO only: nx/ny are the touch position normalized to 0..127
 * across the grid area (already clamped by the caller). Always produces
 * both messages (X and Y), meant to be sent continuously while dragging. */
void lp_mode_xy_event(uint8_t nx, uint8_t ny, lp_midi_msg_t out[2]);

/* LP_MODE_MIXER_CC only: how many fader columns variant `variant` has
 * (0 .. lp_mode_variant_count(LP_MODE_MIXER_CC)-1). This mode has its own
 * dedicated accessor rather than reusing lp_mode_grid_shape()'s {rows,cols}
 * -- fader value resolution is continuous (exact touch Y), not row-
 * quantized, so "rows" has no meaning here anymore. (lp_mode_grid_shape()
 * still returns a {1,columns} shim for this mode, for any generic caller
 * that only wants a column count and doesn't know about this function.) */
int lp_mode_mixer_columns(int variant);

/* LP_MODE_MIXER_CC only: col is 0..lp_mode_mixer_columns(variant)-1, ny is
 * the touch's Y position normalized 0-127 (same top=127 convention as
 * lp_mode_xy_event()'s ny). Meant to be called on press AND continuously
 * while dragging -- never on release (a parked fader keeps its last value,
 * same policy LP_MODE_MIXER_CC always had). */
lp_midi_msg_t lp_mode_mixer_event(int variant, int col, uint8_t ny);

/* LP_MODE_KNOB only: knob_index is row-major (row * cols + col) within
 * lp_mode_grid_shape(LP_MODE_KNOB, variant)'s shape -- this mode DOES use
 * the shared shape table (it's a genuine 2D grid, one knob per cell,
 * unlike Mixer-CC above). value is 0-127, already computed by the caller
 * from a RELATIVE vertical drag (not an absolute touch angle -- see
 * launchpad_app.c's Knob branch for why: no physical detents on a flat
 * touchscreen makes absolute angle-from-touch-position both error-prone
 * and numerically unstable near a knob's center). */
lp_midi_msg_t lp_mode_knob_event(int knob_index, uint8_t value);

#ifdef __cplusplus
}
#endif
