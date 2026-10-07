#include "launchpad_modes.h"

/* Grid-size variants per mode. NOTE and MIXER-CC are formulaic (note/CC
 * numbering is computed from row/col), so any shape works -- these are
 * just a curated set of useful sizes, not the only possible ones. DRUM has
 * exactly one entry: its note mapping is a real GM lookup table keyed to a
 * specific 4x4 layout (see kDrumNotes below), not a formula. XY-macro has
 * exactly one entry: it has no discrete grid at all. */
static const lp_grid_shape_t kNoteVariants[LP_MAX_VARIANTS] = {
    { .rows = 4, .cols = 8 },  /* wide, two octaves across */
    { .rows = 2, .cols = 8 },  /* short, wide -- simple runs */
    { .rows = 4, .cols = 4 },  /* compact square */
};
/* LP_MODE_MIXER_CC's fader-column counts per variant -- see
 * lp_mode_mixer_columns()/lp_mode_mixer_event(). Not a {rows,cols} shape
 * like the table above: fader value resolution is continuous (exact touch
 * Y), not row-quantized, so there's no meaningful "rows" here anymore. */
static const int kMixerColumns[LP_MAX_VARIANTS] = { 4, 8, 2 };

/* LP_MODE_KNOB's grid shapes, one knob per cell -- unlike Mixer-CC this IS
 * a genuine 2D grid (knobs have no continuous-row concept to lose), so it
 * reuses the shared {rows,cols} shape table and every bit of
 * pad_cell_rect()/lp_grid_hit_pad() machinery as-is. */
static const lp_grid_shape_t kKnobVariants[LP_MAX_VARIANTS] = {
    { .rows = 2, .cols = 4 },  /* 8 knobs, default */
    { .rows = 2, .cols = 2 },  /* 4 knobs, large targets */
    { .rows = 4, .cols = 4 },  /* 16 knobs, dense bank */
};

int lp_mode_variant_count(lp_mode_t mode)
{
    switch (mode) {
    case LP_MODE_NOTE:     return LP_MAX_VARIANTS;
    case LP_MODE_MIXER_CC: return LP_MAX_VARIANTS;
    case LP_MODE_KNOB:     return LP_MAX_VARIANTS;
    case LP_MODE_DRUM:     return 1;
    case LP_MODE_XY_MACRO: return 1;
    case LP_MODE_LOOPER:   return 1;
    default:               return 1;
    }
}

lp_grid_shape_t lp_mode_grid_shape(lp_mode_t mode, int variant)
{
    int n = lp_mode_variant_count(mode);
    if (variant < 0 || variant >= n) variant = 0;
    switch (mode) {
    case LP_MODE_NOTE:     return kNoteVariants[variant];
    /* Compat shim for any generic caller that just wants a column count --
     * real Mixer-CC logic (app.c's touch handling, draw_mixer_faders())
     * calls lp_mode_mixer_columns() directly instead. "rows" here is
     * meaningless (see kMixerColumns' own comment) and must not be used
     * for anything. */
    case LP_MODE_MIXER_CC: return (lp_grid_shape_t){ .rows = 1, .cols = kMixerColumns[variant] };
    case LP_MODE_KNOB:     return kKnobVariants[variant];
    case LP_MODE_DRUM:     return (lp_grid_shape_t){ .rows = 4, .cols = 4 };
    case LP_MODE_XY_MACRO: return (lp_grid_shape_t){ .rows = 1, .cols = 1 };
    /* No discrete pads -- same reasoning as XY-macro, see launchpad_grid.c's
     * looper() renderer and lp_loop_*_hit() instead. */
    case LP_MODE_LOOPER:   return (lp_grid_shape_t){ .rows = 1, .cols = 1 };
    default:               return (lp_grid_shape_t){ .rows = 4, .cols = 4 };
    }
}

int lp_mode_mixer_columns(int variant)
{
    if (variant < 0 || variant >= LP_MAX_VARIANTS) variant = 0;
    return kMixerColumns[variant];
}

const char* lp_mode_label(lp_mode_t mode)
{
    switch (mode) {
    case LP_MODE_NOTE:     return "NOTE";
    case LP_MODE_DRUM:     return "DRUM";
    case LP_MODE_MIXER_CC: return "MIXER";
    case LP_MODE_XY_MACRO: return "XY";
    case LP_MODE_KNOB:     return "KNOB";
    case LP_MODE_LOOPER:   return "LOOP";
    default:               return "?";
    }
}

/* Isomorphic layout: pad (row,col) -> base note + row*cols + col, so moving right increases pitch by a semitone and each row below
 * increases it by the current column count. This is the legacy mapping,
 * not a scale-constrained or fourths layout. Base note 36 (C2) is just a
 * reasonable low-register starting point -- Note mode has no fixed
 * instrument, so this isn't a GM convention, just a usable default. */
#define LP_NOTE_BASE 36

static uint8_t note_for_pad(int row, int cols, int col)
{
    int n = LP_NOTE_BASE + row * cols + col;
    if (n < 0)   n = 0;
    if (n > 127) n = 127;
    return (uint8_t)n;
}

/* GM Percussion Key Map (General MIDI 1 Sound Set, channel 10 convention)
 * note numbers -- real, standard values, cross-checked against the GM1
 * spec's percussion table, not placeholders. Laid out bottom-row-first so
 * the most-used sounds (kick/snare/hats) land under the thumb at the
 * bottom of the screen, same reasoning a hardware drum-pad layout uses.
 * Fixed at 4x4 -- see this file's header comment on why DRUM doesn't
 * offer size variants. */
static const uint8_t kDrumNotes[4][4] = {
    /* row 0 (top)    */ { 49, 51, 57, 59 },  /* Crash1, Ride1, Crash2, Ride2 */
    /* row 1          */ { 45, 47, 48, 50 },  /* LowTom, LowMidTom, HiMidTom, HighTom */
    /* row 2          */ { 39, 42, 44, 46 },  /* HandClap, ClosedHat, PedalHat, OpenHat */
    /* row 3 (bottom) */ { 35, 36, 38, 40 },  /* AcousticBassDrum, BassDrum1, AcousticSnare, ElectricSnare */
};

lp_msg_result_t lp_mode_pad_event(lp_mode_t mode, int variant, int pad_index, int pressed)
{
    lp_msg_result_t r = { .valid = 0 };

    lp_grid_shape_t shape = lp_mode_grid_shape(mode, variant);
    if (pad_index < 0 || pad_index >= shape.rows * shape.cols) {
        return r;
    }
    int row = pad_index / shape.cols;
    int col = pad_index % shape.cols;

    switch (mode) {
    case LP_MODE_NOTE: {
        r.valid    = 1;
        r.msg.kind = pressed ? LP_MSG_NOTE_ON : LP_MSG_NOTE_OFF;
        r.msg.number = note_for_pad(row, shape.cols, col);
        /* Fixed velocity -- this touch panel has no confirmed pressure
         * data (see the hardware exploration behind this feature's plan),
         * same approach touch-native control surfaces like TouchOSC/Lemur
         * use for a plain tap. */
        r.msg.value = pressed ? 100 : 0;
        return r;
    }
    case LP_MODE_DRUM: {
        r.valid    = 1;
        r.msg.kind = pressed ? LP_MSG_NOTE_ON : LP_MSG_NOTE_OFF;
        r.msg.number = kDrumNotes[row][col];
        r.msg.value = pressed ? 110 : 0;
        return r;
    }
    /* LP_MODE_MIXER_CC is NOT handled here -- it's continuous-drag, not a
     * discrete press/release pad event; see lp_mode_mixer_event() instead.
     * Falls through to the default no-op if anything stray ever reaches
     * this function for that mode. */
    default:
        return r;
    }
}

lp_midi_msg_t lp_mode_mixer_event(int variant, int col, uint8_t ny)
{
    /* CC20-27 (column 0-7, the widest variant): the "undefined, general
     * purpose" range of the MIDI 1.0 CC table, safe to use without
     * colliding with a conventional CC's usual meaning. ny is already the
     * touch's exact Y position normalized 0-127 (top=127, same convention
     * lp_mode_xy_event()'s ny uses) -- continuous, not row-quantized,
     * matching a real fader's full-resolution throw. */
    int cols = lp_mode_mixer_columns(variant);
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;
    lp_midi_msg_t m = { .kind = LP_MSG_CC, .number = (uint8_t)(20 + col), .value = ny };
    return m;
}

lp_midi_msg_t lp_mode_knob_event(int knob_index, uint8_t value)
{
    /* CC102+index: MIDI 1.0's CC102-119 undefined/general-purpose block --
     * distinct from Mixer-CC's CC20-27 and XY's CC1/CC74, so all three
     * modes can run simultaneously (e.g. a DAW listening for all of them
     * at once) with zero numbering collisions. The densest Knob variant
     * (16 knobs) reaches CC102-117, still inside the block with room to
     * spare. */
    if (knob_index < 0) knob_index = 0;
    lp_midi_msg_t m = { .kind = LP_MSG_CC, .number = (uint8_t)(102 + knob_index), .value = value };
    return m;
}

void lp_mode_xy_event(uint8_t nx, uint8_t ny, lp_midi_msg_t out[2])
{
    /* CC1 (mod wheel) and CC74 (filter cutoff, the de facto "Y axis" in
     * most synth plugins/controllers) -- both real, conventional CC
     * assignments rather than arbitrary numbers, so this is usable
     * against a stock synth patch without remapping anything first. */
    out[0].kind = LP_MSG_CC; out[0].number = 1;  out[0].value = nx;
    out[1].kind = LP_MSG_CC; out[1].number = 74; out[1].value = ny;
}
