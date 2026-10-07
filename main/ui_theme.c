#include "ui_theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGBA8(hexr, hexg, hexb) { (hexr), (hexg), (hexb), 255 }

/* Default fallback theme: compiled into firmware. No SD dependency.
 *
 * Flat, cool, dark "industry" palette -- deliberately matched to the
 * Launchpad screen's own local palette (main/launchpad_grid.c's kBg/
 * kTabActive/kStatusGood et al) so the whole firmware reads as one
 * coherent hardware-controller UI instead of Launchpad's modern screen
 * sitting next to Settings/Diagnostics' old warm amber/gold "terminal"
 * look. Note most of the actual retro SIGNALING in this codebase (CRT
 * scanlines/vignette/flicker/noise in orion_ui_effects_t, and the
 * rivets/corner-chamfers/hazard-stripes ui_components.c draws when
 * chrome_style==ORION_UI_CHROME_VAULT_DECO) was already off/unused here --
 * chrome_style stays ORION_UI_CHROME_ATOMIC (the plain, undecorated path)
 * and every effect field below stays zeroed, same as before. The "old
 * Orion" look this replaces was carried almost entirely by color alone. */
static const orion_ui_theme_t kIndustryDark = {
    .schema_version = ORION_THEME_SCHEMA_VERSION,
    .name = "Industry Dark",

    .bg          = RGBA8(0x10, 0x10, 0x14),
    .panel       = RGBA8(0x22, 0x24, 0x2C),
    .panel_hi    = RGBA8(0x2E, 0x32, 0x3C),
    .edge        = RGBA8(0x3C, 0x40, 0x4C),
    .edge_hi     = RGBA8(0x5A, 0x60, 0x70),
    .text        = RGBA8(0xE6, 0xE6, 0xEB),
    .dim         = RGBA8(0x82, 0x86, 0x91),
    .accent      = RGBA8(0x40, 0x80, 0xFF),
    .accent_hi   = RGBA8(0x78, 0xAA, 0xFF),
    .good        = RGBA8(0x3C, 0xC8, 0x6E),
    .warning     = RGBA8(0xE6, 0xAA, 0x28),
    .danger      = RGBA8(0xDC, 0x3C, 0x3C),
    /* Deliberately impossible to mistake for finished art. */
    .placeholder = RGBA8(0xFF, 0x00, 0xA8),

    .metrics = {
        .margin = 24,
        .gap = 14,
        .topbar_h = 64,
        .sidebar_w = 202,
        .card_h = 138,
        .row_h = 64,
        .touch_min = 72,
        .border_w = 1,
        .accent_rail_w = 5,
        .radius = 4,
        .title_scale = 4,
        .body_scale = 2,
        .small_scale = 1,
        .scanline_alpha = 0,
        .selection_style = ORION_UI_SELECT_RAIL,
    },
    .style = {
        .chrome_style = ORION_UI_CHROME_ATOMIC,
        .background_pattern = ORION_UI_BG_SOLID,
        .layout_preset = ORION_UI_LAYOUT_STANDARD,
        .ornament_density = 0,
        .rivet_size = 0,
        .corner_cut = 0,
        .inner_border = 0,
        .shadow_px = 0,
        .hazard_stripes = 0,
        .grid_pitch = 24,
        .panel_pad = 14,
        .title_plate_h = 18,
        .grid_min_cell_w = 220,
    },
    .effects = {
        .overlay = ORION_UI_OVERLAY_NONE,
        .scanlines = 0,
        .scanline_strength = 0,
        .scanline_pitch = 4,
        .phosphor_glow = 0,
        .glow_strength = 0,
        .glow_radius = 1,
        .glow_color = RGBA8(0x9F, 0xD3, 0x6A),
        .vignette = 0,
        .vignette_strength = 0,
        .vignette_width = 56,
        .flicker = 0,
        .flicker_strength = 0,
        .flicker_rate = 7,
        .refresh_bar = 0,
        .refresh_bar_speed = 24,
        .refresh_bar_width = 28,
        .noise = 0,
        .noise_strength = 0,
        .noise_density = 0,
        .horizontal_jitter = 0,
        .jitter_amount = 0,
        .jitter_rate = 3,
        .artifact_rate = 0,
        .artifact_strength = 0,
    },
};

/* UI size control. s_base holds the last loaded/reset theme exactly as parsed;
 * s_active -- what orion_ui_theme() actually returns, and therefore what
 * every screen in the app already reads its geometry/text-scale from with
 * zero code changes -- is s_base with the current step's multiplier
 * applied. Reapplied on every theme change so switching themes doesn't
 * silently reset the user's chosen size, and vice versa. Modest steps
 * (max +30%) deliberately: the whole app was just verified overlap-free at
 * 1.0x in both orientations, and a large jump would need re-verifying that
 * from scratch. border_w/accent_rail_w/radius/scanline_alpha are left
 * unscaled -- hairline widths and an alpha value, not "size". */
static orion_ui_theme_t s_base;
static orion_ui_theme_t s_active;
static int s_initialized;
static int s_scale_step; /* 0=normal 1=large 2=x-large */

static const float kUiScaleMult[3] = { 1.00f, 1.15f, 1.30f };

static int scale_round(int v, float mult)
{
    return (int)(v * mult + 0.5f);
}

/* The font-scale fields (title/body/small_scale) are tiny integers
 * (4/2/1 by default), and scale_round()'s round-to-nearest silently loses
 * a whole LARGE step for them --
 * 1*1.15+0.5=1.65 still truncates to 1, so small_scale (and, at the
 * default theme's values, body_scale too) never actually changed at the
 * LARGE step, only at X-LARGE, and small_scale never changed at ALL
 * across either step. Geometry fields (margin/card_h/etc.) don't have this
 * problem since their base values are already large enough that
 * round-to-nearest always moves them. Text-scale fields need their own
 * rule: any mult>1.0 must produce a strictly larger integer, even if that
 * means rounding up more aggressively than round-to-nearest would. */
static int scale_text(int v, float mult)
{
    if (mult <= 1.0f) return v;
    int r = (int)((float)v * mult + 0.999f); /* ceil, without <math.h> */
    return r > v ? r : v + 1;
}

static void apply_ui_scale(void)
{
    s_active = s_base;
    float mult = kUiScaleMult[s_scale_step];
    orion_ui_metrics_t* m = &s_active.metrics;
    const orion_ui_metrics_t* b = &s_base.metrics;
    m->margin      = scale_round(b->margin, mult);
    m->gap         = scale_round(b->gap, mult);
    m->topbar_h    = scale_round(b->topbar_h, mult);
    m->card_h      = scale_round(b->card_h, mult);
    m->row_h       = scale_round(b->row_h, mult);
    m->touch_min   = scale_round(b->touch_min, mult);
    m->title_scale = scale_text(b->title_scale, mult);
    if (m->title_scale < 1) m->title_scale = 1;
    m->body_scale  = scale_text(b->body_scale, mult);
    if (m->body_scale < 1) m->body_scale = 1;
    m->small_scale = scale_text(b->small_scale, mult);
    if (m->small_scale < 1) m->small_scale = 1;

    /* Schema-2 decorative geometry follows UI size too, while booleans and
     * enum choices remain literal. This keeps ornate themes coherent at the
     * user's LARGE/X-LARGE settings instead of leaving tiny fixed rivets and
     * title plates floating inside otherwise scaled chrome. */
    s_active.style.corner_cut = scale_round(s_base.style.corner_cut, mult);
    s_active.style.shadow_px = scale_round(s_base.style.shadow_px, mult);
    s_active.style.grid_pitch = scale_round(s_base.style.grid_pitch, mult);
    s_active.style.panel_pad = scale_round(s_base.style.panel_pad, mult);
    s_active.style.title_plate_h = scale_round(s_base.style.title_plate_h, mult);
    s_active.style.grid_min_cell_w = scale_round(s_base.style.grid_min_cell_w, mult);

    /* Schema-3 effect geometry that is expressed in pixels follows the same
     * accessibility/UI-size scale. Strength/rate fields stay literal. */
    s_active.effects.scanline_pitch = scale_round(s_base.effects.scanline_pitch, mult);
    if (s_active.effects.scanline_pitch < 2) s_active.effects.scanline_pitch = 2;
    s_active.effects.glow_radius = scale_round(s_base.effects.glow_radius, mult);
    if (s_active.effects.glow_radius > 2) s_active.effects.glow_radius = 2;
    s_active.effects.vignette_width = scale_round(s_base.effects.vignette_width, mult);
    s_active.effects.refresh_bar_width = scale_round(s_base.effects.refresh_bar_width, mult);
    s_active.effects.jitter_amount = scale_round(s_base.effects.jitter_amount, mult);
}

static void ensure_initialized(void)
{
    if (!s_initialized) {
        s_base = kIndustryDark;
        s_initialized = 1;
        apply_ui_scale();
    }
}

void orion_ui_set_scale(int step)
{
    ensure_initialized();
    if (step < 0) step = 0;
    if (step > 2) step = 2;
    s_scale_step = step;
    apply_ui_scale();
}

int orion_ui_get_scale(void)
{
    return s_scale_step;
}

int orion_ui_scale_px(int base_px)
{
    ensure_initialized();
    return scale_round(base_px, kUiScaleMult[s_scale_step]);
}

static int s_grid_cols = 2;

void orion_ui_set_grid_cols(int cols)
{
    if (cols < 1) cols = 1;
    s_grid_cols = cols;
}

int orion_ui_get_grid_cols(void)
{
    return s_grid_cols;
}

/* Topbar RAM-usage badge on/off. Lives here
 * (not in ui_components.c, which owns topbar_mem()'s per-frame draw state)
 * for the same reason UI_SIZE/grid_cols do: a plain persisted preference,
 * not draw-time state -- touchmenu.c's poll loop reads this once a second
 * and pushes the result into ui_components.c's orion_ui_topbar_set_mem(). */
static int s_mem_badge_enabled = 0;

void orion_ui_set_mem_badge(int enabled)
{
    s_mem_badge_enabled = enabled ? 1 : 0;
}

int orion_ui_get_mem_badge(void)
{
    return s_mem_badge_enabled;
}

/* Idle sleep timeout. Index into a fixed 3-way
 * preset (0=OFF, 1=2MIN, 2=10MIN), same "plain persisted preference read by
 * the app layer" shape as grid_cols/mem_badge above -- the actual idle
 * timer and sleep/wake logic live in touchmenu.c, this is just the stored
 * choice. Default OFF: sleep is an opt-in behavior change, not something a
 * fresh boot should silently start doing. */
static int s_sleep_timeout_idx = 0;

void orion_ui_set_sleep_timeout_idx(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    s_sleep_timeout_idx = idx;
}

int orion_ui_get_sleep_timeout_idx(void)
{
    return s_sleep_timeout_idx;
}

/* Real seconds for each preset -- 0 means "disabled" (the idle timer never
 * fires), shared by the settings UI and the launcher's own idle check so
 * they can never disagree on what "2MIN" actually means. */
int orion_ui_sleep_timeout_s(void)
{
    static const int kSecs[3] = { 0, 120, 600 };
    return kSecs[s_sleep_timeout_idx];
}

const orion_ui_theme_t* orion_ui_theme_builtin_industry_dark(void)
{
    return &kIndustryDark;
}

const orion_ui_theme_t* orion_ui_theme(void)
{
    ensure_initialized();
    return &s_active;
}

void orion_ui_theme_reset_default(void)
{
    s_base = kIndustryDark;
    s_initialized = 1;
    apply_ui_scale();
}

static char* trim(char* s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

static int parse_hex_color(const char* s, sgfx_rgba8_t* out)
{
    if (!s || !out) return -1;
    if (*s == '#') ++s;
    if (strlen(s) != 6) return -1;
    char* end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (!end || *end != '\0' || v > 0xFFFFFFul) return -1;
    out->r = (uint8_t)(v >> 16);
    out->g = (uint8_t)(v >> 8);
    out->b = (uint8_t)v;
    out->a = 255;
    return 0;
}

static int parse_int_range(const char* s, int lo, int hi, int* out)
{
    if (!s || !out) return -1;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *trim(end) != '\0' || v < lo || v > hi) return -1;
    *out = (int)v;
    return 0;
}

static int assign_color(orion_ui_theme_t* t, const char* key, const char* val)
{
    struct color_binding { const char* key; sgfx_rgba8_t* color; } bindings[] = {
        { "bg", &t->bg }, { "panel", &t->panel }, { "panel_hi", &t->panel_hi },
        { "edge", &t->edge }, { "edge_hi", &t->edge_hi }, { "text", &t->text },
        { "dim", &t->dim }, { "accent", &t->accent }, { "accent_hi", &t->accent_hi },
        { "good", &t->good }, { "warning", &t->warning }, { "danger", &t->danger },
        { "placeholder", &t->placeholder }, { "glow_color", &t->effects.glow_color },
    };
    for (size_t i = 0; i < sizeof bindings / sizeof bindings[0]; ++i) {
        if (strcmp(key, bindings[i].key) == 0) return parse_hex_color(val, bindings[i].color);
    }
    return 1; /* not a color key */
}

static int assign_metric(orion_ui_theme_t* t, const char* key, const char* val)
{
    struct metric_binding { const char* key; int* field; int lo, hi; } bindings[] = {
        { "margin", &t->metrics.margin, 8, 96 },
        { "gap", &t->metrics.gap, 4, 64 },
        { "topbar_h", &t->metrics.topbar_h, 40, 110 },
        { "sidebar_w", &t->metrics.sidebar_w, 120, 420 },
        { "card_h", &t->metrics.card_h, 72, 220 },
        { "row_h", &t->metrics.row_h, 44, 160 },
        { "touch_min", &t->metrics.touch_min, 48, 96 },
        { "border_w", &t->metrics.border_w, 1, 6 },
        { "accent_rail_w", &t->metrics.accent_rail_w, 2, 16 },
        { "radius", &t->metrics.radius, 0, 24 },
        { "title_scale", &t->metrics.title_scale, 2, 8 },
        { "body_scale", &t->metrics.body_scale, 1, 5 },
        { "small_scale", &t->metrics.small_scale, 1, 3 },
        { "scanline_alpha", &t->metrics.scanline_alpha, 0, 80 },
    };
    for (size_t i = 0; i < sizeof bindings / sizeof bindings[0]; ++i) {
        if (strcmp(key, bindings[i].key) == 0)
            return parse_int_range(val, bindings[i].lo, bindings[i].hi, bindings[i].field);
    }
    if (strcmp(key, "selection_style") == 0) {
        if (strcmp(val, "rail") == 0) t->metrics.selection_style = ORION_UI_SELECT_RAIL;
        else if (strcmp(val, "frame") == 0) t->metrics.selection_style = ORION_UI_SELECT_FRAME;
        else if (strcmp(val, "invert") == 0) t->metrics.selection_style = ORION_UI_SELECT_INVERT;
        else return -1;
        return 0;
    }
    return 1;
}

static int assign_style(orion_ui_theme_t* t, const char* key, const char* val)
{
    struct int_binding { const char* key; int* field; int lo, hi; } ints[] = {
        { "ornament_density", &t->style.ornament_density, 0, 3 },
        { "rivet_size", &t->style.rivet_size, 0, 4 },
        { "corner_cut", &t->style.corner_cut, 0, 18 },
        { "inner_border", &t->style.inner_border, 0, 1 },
        { "shadow_px", &t->style.shadow_px, 0, 8 },
        { "hazard_stripes", &t->style.hazard_stripes, 0, 1 },
        { "grid_pitch", &t->style.grid_pitch, 8, 80 },
        { "panel_pad", &t->style.panel_pad, 4, 32 },
        { "title_plate_h", &t->style.title_plate_h, 14, 40 },
        { "grid_min_cell_w", &t->style.grid_min_cell_w, 160, 420 },
    };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; ++i) {
        if (strcmp(key, ints[i].key) == 0)
            return parse_int_range(val, ints[i].lo, ints[i].hi, ints[i].field);
    }
    if (strcmp(key, "chrome_style") == 0) {
        if (strcmp(val, "atomic") == 0) t->style.chrome_style = ORION_UI_CHROME_ATOMIC;
        else if (strcmp(val, "vault_deco") == 0) t->style.chrome_style = ORION_UI_CHROME_VAULT_DECO;
        else return -1;
        return 0;
    }
    if (strcmp(key, "background_pattern") == 0) {
        if (strcmp(val, "solid") == 0) t->style.background_pattern = ORION_UI_BG_SOLID;
        else if (strcmp(val, "grid") == 0) t->style.background_pattern = ORION_UI_BG_GRID;
        else if (strcmp(val, "dots") == 0) t->style.background_pattern = ORION_UI_BG_DOTS;
        else return -1;
        return 0;
    }
    if (strcmp(key, "layout_preset") == 0) {
        if (strcmp(val, "standard") == 0) t->style.layout_preset = ORION_UI_LAYOUT_STANDARD;
        else if (strcmp(val, "instrument") == 0) t->style.layout_preset = ORION_UI_LAYOUT_INSTRUMENT;
        else return -1;
        return 0;
    }
    return 1;
}

static int assign_effect(orion_ui_theme_t* t, const char* key, const char* val)
{
    struct int_binding { const char* key; int* field; int lo, hi; } ints[] = {
        { "scanlines", &t->effects.scanlines, 0, 1 },
        { "scanline_strength", &t->effects.scanline_strength, 0, 80 },
        { "scanline_pitch", &t->effects.scanline_pitch, 2, 8 },
        { "phosphor_glow", &t->effects.phosphor_glow, 0, 1 },
        { "glow_strength", &t->effects.glow_strength, 0, 100 },
        { "glow_radius", &t->effects.glow_radius, 0, 2 },
        { "vignette", &t->effects.vignette, 0, 1 },
        { "vignette_strength", &t->effects.vignette_strength, 0, 80 },
        { "vignette_width", &t->effects.vignette_width, 8, 160 },
        { "flicker", &t->effects.flicker, 0, 1 },
        { "flicker_strength", &t->effects.flicker_strength, 0, 24 },
        { "flicker_rate", &t->effects.flicker_rate, 1, 20 },
        { "refresh_bar", &t->effects.refresh_bar, 0, 1 },
        { "refresh_bar_speed", &t->effects.refresh_bar_speed, 4, 120 },
        { "refresh_bar_width", &t->effects.refresh_bar_width, 4, 96 },
        { "noise", &t->effects.noise, 0, 1 },
        { "noise_strength", &t->effects.noise_strength, 0, 32 },
        { "noise_density", &t->effects.noise_density, 0, 12 },
        { "horizontal_jitter", &t->effects.horizontal_jitter, 0, 1 },
        { "jitter_amount", &t->effects.jitter_amount, 0, 6 },
        { "jitter_rate", &t->effects.jitter_rate, 1, 20 },
        { "artifact_rate", &t->effects.artifact_rate, 0, 20 },
        { "artifact_strength", &t->effects.artifact_strength, 0, 24 },
    };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; ++i) {
        if (strcmp(key, ints[i].key) == 0)
            return parse_int_range(val, ints[i].lo, ints[i].hi, ints[i].field);
    }
    if (strcmp(key, "overlay") == 0) {
        if (strcmp(val, "none") == 0) t->effects.overlay = ORION_UI_OVERLAY_NONE;
        else if (strcmp(val, "crt") == 0) t->effects.overlay = ORION_UI_OVERLAY_CRT;
        else return -1;
        return 0;
    }
    return 1;
}

int orion_ui_theme_load_file(const char* path)
{
    if (!path || !*path) return -1;
    ensure_initialized();

    FILE* fp = fopen(path, "r");
    if (!fp) return -2;

    /* Parse on top of the guaranteed fallback, not the previous custom theme.
     * Partial themes are therefore deterministic and missing keys never retain
     * stale values from a different custom theme. */
    orion_ui_theme_t candidate = kIndustryDark;
    char line[192];
    int saw_schema = 0;
    int errors = 0;

    while (fgets(line, sizeof line, fp)) {
        char* p = trim(line);
        if (!*p || *p == '#' || *p == ';' || *p == '[') continue;
        char* eq = strchr(p, '=');
        if (!eq) { ++errors; continue; }
        *eq = '\0';
        char* key = trim(p);
        char* val = trim(eq + 1);

        if (strcmp(key, "schema") == 0) {
            int v = 0;
            if (parse_int_range(val, 1, ORION_THEME_SCHEMA_VERSION, &v) != 0) ++errors;
            else { candidate.schema_version = v; saw_schema = 1; }
            continue;
        }
        if (strcmp(key, "name") == 0) {
            /* Explicit precision: `val` is parsed from a theme file on SD,
             * unbounded from the compiler's view -- see ymodem.c's own
             * comment on the same -Wformat-truncation pattern. */
            snprintf(candidate.name, sizeof candidate.name, "%.31s", val);
            continue;
        }

        int rc = assign_color(&candidate, key, val);
        if (rc == 1) rc = assign_metric(&candidate, key, val);
        if (rc == 1) rc = assign_style(&candidate, key, val);
        if (rc == 1) rc = assign_effect(&candidate, key, val);
        /* Unknown keys are intentionally ignored for forward compatibility;
         * known keys with invalid values are errors. */
        if (rc < 0) ++errors;
    }
    fclose(fp);

    if (!saw_schema || errors) return -3;
    s_base = candidate;
    apply_ui_scale();
    return 0;
}

int orion_ui_theme_reload_sd(void)
{
    return orion_ui_theme_load_file(ORION_THEME_DEFAULT_SD_PATH);
}

#undef RGBA8
