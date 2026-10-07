#pragma once

#include <stdint.h>
#include "sgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORION_THEME_SCHEMA_VERSION 3
#define ORION_THEME_DEFAULT_SD_PATH "/sdcard/orion/theme.ini"

typedef enum {
    ORION_UI_SELECT_RAIL = 0,
    ORION_UI_SELECT_FRAME = 1,
    ORION_UI_SELECT_INVERT = 2,
} orion_ui_selection_style_t;

typedef struct {
    int margin;
    int gap;
    int topbar_h;
    int sidebar_w;
    int card_h;
    int row_h;
    int touch_min;
    int border_w;
    int accent_rail_w;
    int radius;            /* SGFX is rect-only today; retained for future renderer. */
    int title_scale;
    int body_scale;
    int small_scale;
    int scanline_alpha;    /* 0..255; default fallback keeps this subtle/off. */
    orion_ui_selection_style_t selection_style;
} orion_ui_metrics_t;

typedef enum {
    ORION_UI_CHROME_ATOMIC = 0,
    ORION_UI_CHROME_VAULT_DECO = 1,
} orion_ui_chrome_style_t;

typedef enum {
    ORION_UI_BG_SOLID = 0,
    ORION_UI_BG_GRID = 1,
    ORION_UI_BG_DOTS = 2,
} orion_ui_background_pattern_t;

typedef enum {
    ORION_UI_LAYOUT_STANDARD = 0,
    ORION_UI_LAYOUT_INSTRUMENT = 1,
} orion_ui_layout_preset_t;

typedef struct {
    orion_ui_chrome_style_t chrome_style;
    orion_ui_background_pattern_t background_pattern;
    orion_ui_layout_preset_t layout_preset;
    int ornament_density;   /* 0..3 */
    int rivet_size;         /* 0 disables, otherwise 1..4 px */
    int corner_cut;         /* visual chamfer length, 0..18 px */
    int inner_border;       /* 0/1 */
    int shadow_px;          /* decorative panel shadow, 0..8 px */
    int hazard_stripes;     /* 0/1 */
    int grid_pitch;         /* background pattern spacing */
    int panel_pad;          /* common interior breathing room */
    int title_plate_h;      /* title/header plate height */
    int grid_min_cell_w;    /* responsive card floor before cols collapse */
} orion_ui_style_t;

typedef enum {
    ORION_UI_OVERLAY_NONE = 0,
    ORION_UI_OVERLAY_CRT = 1,
} orion_ui_overlay_t;

/* Schema-3 display effects. These are deliberately separate from chrome and layout: a theme
 * can use the same geometry with a clean LCD, phosphor CRT, damaged
 * terminal, etc. Dynamic effects are designed around small dirty bands
 * rather than a second full-screen history framebuffer. */
typedef struct {
    orion_ui_overlay_t overlay;
    int scanlines;             /* 0/1 */
    int scanline_strength;     /* 0..80, RGB565 luminance reduction */
    int scanline_pitch;        /* 2..8 px */
    int phosphor_glow;         /* 0/1: text halo */
    int glow_strength;         /* 0..100 */
    int glow_radius;           /* 0..2 px */
    sgfx_rgba8_t glow_color;
    int vignette;              /* 0/1 */
    int vignette_strength;     /* 0..80 */
    int vignette_width;        /* 8..160 px */
    int flicker;               /* 0/1 */
    int flicker_strength;      /* 0..24 */
    int flicker_rate;          /* 1..20 Hz-ish phase rate */
    int refresh_bar;           /* 0/1 */
    int refresh_bar_speed;     /* 4..120 px/s */
    int refresh_bar_width;     /* 4..96 px */
    int noise;                 /* 0/1 */
    int noise_strength;        /* 0..32 */
    int noise_density;         /* 0..12 sparse points per 1k px in band */
    int horizontal_jitter;     /* 0/1 */
    int jitter_amount;         /* 0..6 px */
    int jitter_rate;           /* 1..20 */
    int artifact_rate;         /* 0..20 deterministic event frequency */
    int artifact_strength;     /* 0..24 */
} orion_ui_effects_t;

typedef struct {
    int schema_version;
    char name[32];

    sgfx_rgba8_t bg;
    sgfx_rgba8_t panel;
    sgfx_rgba8_t panel_hi;
    sgfx_rgba8_t edge;
    sgfx_rgba8_t edge_hi;
    sgfx_rgba8_t text;
    sgfx_rgba8_t dim;
    sgfx_rgba8_t accent;
    sgfx_rgba8_t accent_hi;
    sgfx_rgba8_t good;
    sgfx_rgba8_t warning;
    sgfx_rgba8_t danger;
    sgfx_rgba8_t placeholder;

    orion_ui_metrics_t metrics;
    orion_ui_style_t style;
    orion_ui_effects_t effects;
} orion_ui_theme_t;

/* Always available and completely self-contained in flash/rodata. */
const orion_ui_theme_t* orion_ui_theme_builtin_industry_dark(void);

/* Active theme. Starts as the compiled Industry Dark fallback. */
const orion_ui_theme_t* orion_ui_theme(void);
void orion_ui_theme_reset_default(void);

/* Optional override. This never invalidates the built-in fallback: parsing is
 * transactional into a temporary copy and only replaces the active theme on
 * success. Missing SD/card/file therefore changes nothing. */
int orion_ui_theme_load_file(const char* path);
int orion_ui_theme_reload_sd(void);

/* UI size: 0=normal 1=large 2=x-large. Scales text/geometry
 * metrics on top of whichever theme is active -- see ui_theme.c's
 * apply_ui_scale() for exactly what's scaled and why. Every screen reads
 * this for free through orion_ui_theme()'s existing metrics fields; no
 * screen needs to know this exists. */
void orion_ui_set_scale(int step);
int  orion_ui_get_scale(void);

/* Scales a raw pixel constant by the same multiplier applied to the
 * metrics fields above -- for call sites that draw a fixed-size element
 * (an icon, a toggle track, a badge height) that isn't itself one of the
 * named metrics fields but still needs to grow with the UI-size setting so
 * it doesn't visually clip against text that DOES scale. */
int orion_ui_scale_px(int base_px);

/* Grid columns: only ONE axis is ever a locked user preference -- rows are
 * always auto-computed from however many items actually exist, so this
 * keeps working correctly as the item count evolves (e.g. the launcher
 * going from 4 apps to 5) with no setting to revisit. This column count is
 * literal and orientation-independent -- "1" always means a true single
 * column, "3" always means up to 3 columns, subject only to a real
 * min-cell-width floor that can reduce (never increase) it on a narrow
 * screen. */
void orion_ui_set_grid_cols(int cols);
int  orion_ui_get_grid_cols(void);

/* Topbar RAM-usage badge on/off (default off). Plain persisted preference,
 * same shape as the two above -- see ui_components.c's topbar_mem() for
 * what actually gets drawn. */
void orion_ui_set_mem_badge(int enabled);
int  orion_ui_get_mem_badge(void);

/* Idle sleep timeout preset (default OFF, index 0). See ui_theme.c's own
 * comment for the real seconds each index means. */
void orion_ui_set_sleep_timeout_idx(int idx);
int  orion_ui_get_sleep_timeout_idx(void);
int  orion_ui_sleep_timeout_s(void);

#ifdef __cplusplus
}
#endif
