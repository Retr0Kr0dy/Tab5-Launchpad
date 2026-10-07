#pragma once

#include "sgfx.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int x, y, w, h; } orion_ui_rect_t;

/* Item-count-driven grid layout: `count` is
 * the actual number of cells to lay out. The column count is the single
 * global preference set via orion_ui_set_grid_cols() -- literal and the
 * SAME in both orientations (no portrait/landscape transpose -- an earlier
 * version tried that and it inverted the user's intent at the extremes,
 * see orion_ui_grid_rect()'s own comment). The ROW count is always
 * auto-computed as ceil(count/cols), so this keeps producing a correct,
 * gap-free layout no matter how `count` changes over time; no caller ever
 * needs to know or store a full "shape". A real min-cell-width floor still
 * auto-reduces columns (never increases) if the configured count would
 * make cells unreadably narrow on the current screen width. area is the
 * region the grid may fill (chrome/margins already excluded by the
 * caller); gap is inserted between cells on both axes. */
/* max_cell_h: 0 means fill the available height evenly across rows (the
 * original behavior); a positive value makes each cell's height FIXED at
 * that value instead, so a handful of items don't get artificially
 * stretched to occupy the whole screen just because there's room, and so
 * a screen with more items than fit can offer a real scrollbar (dividing
 * evenly would make that undetectable -- the "overflow" would just quietly
 * shrink every cell instead). scroll_px shifts every cell's Y position by
 * a constant offset -- the same value orion_ui_grid_max_scroll() below
 * computes a valid clamp range for -- so a caller doing drag-to-scroll only
 * ever needs to track one int. Pass 0 for a non-scrolling grid. */
orion_ui_rect_t orion_ui_grid_rect(orion_ui_rect_t area, int count,
                                   int gap, int max_cell_h, int index, int scroll_px);

/* How far scroll_px above can go (in px) before the LAST row's bottom edge
 * would clear the top of `area` -- i.e. the real overflow amount, 0 if
 * everything already fits. Same cols/rows math as orion_ui_grid_rect()
 * itself (kept in lockstep deliberately -- computing this any other way
 * risks it drifting out of sync with what actually got laid out). */
int orion_ui_grid_max_scroll(orion_ui_rect_t area, int count, int gap, int max_cell_h);

typedef enum {
    ORION_ICON_TERMINAL = 0,
    ORION_ICON_VIDEO,
    ORION_ICON_SETTINGS,
    ORION_ICON_DISPLAY,
    ORION_ICON_HARDWARE,
    ORION_ICON_NETWORK,
    ORION_ICON_DIAGNOSTICS,
    ORION_ICON_THEME,
    ORION_ICON_STORAGE,
    ORION_ICON_KEYBOARD,
    ORION_ICON_INFO,
    ORION_ICON_AUDIO,
    ORION_ICON_CAMERA,
    ORION_ICON_POWER,
    ORION_ICON_GAMEPAD,
    ORION_ICON_COUNT
} orion_ui_icon_t;

typedef enum {
    ORION_FRAME_STANDARD = 0,
    ORION_FRAME_TITLE,
    ORION_FRAME_NOTCHED,
    ORION_FRAME_BRACKET,
} orion_ui_frame_style_t;

typedef enum {
    ORION_ORNAMENT_ATOM = 0,
    ORION_ORNAMENT_RADAR,
    ORION_ORNAMENT_CHEVRON,
    ORION_ORNAMENT_STARBURST,
} orion_ui_ornament_t;

void orion_ui_clear(sgfx_device_t* d);
void orion_ui_topbar_set_power(int percent, int charging, int present);
void orion_ui_topbar_set_power_unknown(void);
void orion_ui_topbar(sgfx_device_t* d, int w, const char* path,
                     const char* right_status);
/* Redraws ONLY the battery/CHG badge in the topbar's top-right corner --
 * not the path text, not right_status, not the topbar background strip.
 * For periodic idle-screen refreshes where only the power state may have
 * changed; callers must NOT precede this with orion_ui_clear() or any
 * other redraw, since that would defeat the whole point (see the .c file's
 * own comment). */
void orion_ui_topbar_power_only(sgfx_device_t* d, int w);
/* Optional RAM-usage badge next to the battery/CHG group. `enabled`
 * mirrors the Settings toggle; when off, topbar_mem() draws nothing and
 * reserves no width (right_status's own floor shifts back automatically --
 * see topbar_mem_group_width()'s call sites in the .c file). `used_pct` is
 * heap-used percent, app layer's call, same "setter written by the poll
 * loop, read internally at draw time" shape as orion_ui_topbar_set_power(). */
void orion_ui_topbar_set_mem(int enabled, int used_pct);
/* Same periodic-partial-redraw contract as orion_ui_topbar_power_only(). */
void orion_ui_topbar_mem_only(sgfx_device_t* d, int w);
void orion_ui_panel(sgfx_device_t* d, orion_ui_rect_t r, int selected);
void orion_ui_button(sgfx_device_t* d, orion_ui_rect_t r, const char* label,
                     const char* secondary, int selected, int destructive);
void orion_ui_status_chip(sgfx_device_t* d, orion_ui_rect_t r,
                          const char* text, sgfx_rgba8_t color);
void orion_ui_icon(sgfx_device_t* d, orion_ui_icon_t icon, int x, int y,
                   int size, sgfx_rgba8_t color);
void orion_ui_missing_asset(sgfx_device_t* d, orion_ui_rect_t r, const char* name);
void orion_ui_debug_get_icon_master(orion_ui_icon_t icon, int master, uint8_t* out_pixels);
/* Final full-frame theme overlay. Historical name retained for source
 * compatibility; schema-3 expands it from plain scanlines into the static CRT
 * compositor (scanline luminance, vignette and static phosphor/noise pass). */
void orion_ui_scanlines(sgfx_device_t* d, int w, int h);

/* Region-scoped scanline/vignette reapplication -- a partial redraw (e.g. a
 * scroll) can otherwise erase the CRT texture in just that region.
 * px/py/pw/ph are PHYSICAL scanout coordinates -- the caller (a firmware-
 * only file with orion_display_logical_to_phys() available) converts,
 * keeping this file's own host-buildability unchanged. See its own comment
 * in ui_components.c for why a scoped call, not orion_ui_scanlines()
 * itself, is what a partial/scroll redraw needs. */
void orion_ui_scanlines_phys_region(sgfx_device_t* d, int px, int py, int pw, int ph);

/* True delta-redraw shift primitive. Shifts a
 * PHYSICAL region by phys_shift pixels along one axis (0=rows, 1=within-row
 * columns) so already-drawn pixels move to their new position instead of
 * being redrawn -- caller only needs to paint the strip newly exposed at
 * the leading edge. Returns 0 (fall back to a full redraw) on a non-
 * direct-framebuffer target or a shift too large to leave anything in
 * place. See its own comment in ui_components.c for the full contract. */
int orion_ui_fb_shift_phys_region(sgfx_device_t* d, int px, int py, int pw, int ph,
                                  int axis_is_x, int phys_shift);

/* Reset dynamic overlay state after the underlying screen has been redrawn.
 * orion_ui_clear() already calls this, so normal screens never need to. */
void orion_ui_effects_reset(void);
void orion_ui_effects_prepare_draw(sgfx_device_t* d);
/* Animate the moving CRT layer using only a narrow backed-up framebuffer band.
 * Returns nonzero when pixels changed. On direct-FB Tab5 panels the function
 * restores the previous band, captures the next one, applies sweep/flicker/
 * noise/jitter/artifacts, and cache-flushes only those dirty rectangles. */
int  orion_ui_effects_tick(sgfx_device_t* d, uint32_t now_ms);
/* One runtime animation entry point for firmware poll loops: advances marquee
 * text and schema-3 display effects from the same clock. */
void orion_ui_animation_tick(sgfx_device_t* d, uint32_t now_ms);

/* Reusable chrome pack (panels/buttons/badges/tabs). Combined with
 * pictograms above this forms the default asset family used by the
 * catalog preview. */
void orion_ui_frame(sgfx_device_t* d, orion_ui_rect_t r,
                    orion_ui_frame_style_t style, int selected, int disabled,
                    const char* title);
void orion_ui_tab(sgfx_device_t* d, orion_ui_rect_t r, const char* text,
                  int selected, int disabled);
/* bg: the toggle draws its own label text but not a background fill behind
 * it (the row belongs to whatever container the caller already painted --
 * the plain screen background in most places, a panel in others like the
 * theme gallery) -- pass whatever color is actually behind this rect so
 * the carousel text can clear-and-redraw itself correctly if it overflows. */
void orion_ui_toggle(sgfx_device_t* d, orion_ui_rect_t r, const char* text,
                     int on, int disabled, sgfx_rgba8_t bg);
/* Caller already has the exact rect it drew the toggle with; this is a
 * plain point-in-rect test against that same rect, no recomputation. */
int orion_ui_toggle_hit(orion_ui_rect_t r, int x, int y);
/* bg: same reasoning as orion_ui_toggle()'s bg param above -- the meter's
 * label/value text sits above the bar it draws itself, on whatever
 * background the caller already painted. */
void orion_ui_meter(sgfx_device_t* d, orion_ui_rect_t r, const char* label,
                    int value_pct, sgfx_rgba8_t accent, int segmented, sgfx_rgba8_t bg);
/* True delta redraw for orion_ui_meter()'s SEGMENTED style, for live drags
 * -- only repaints segments whose lit state actually changed between
 * prev_pct and pct, never the shared background/border. prev_pct<0 means
 * "no previous value" (caller should use full orion_ui_meter() instead for
 * that first draw).
 *
 * out_seg_rect/out_text_rect: the two DISJOINT logical rects actually
 * touched (segment range, percentage text) -- either may be NULL if the
 * caller doesn't need it, and out_seg_rect comes back w==0 when no segment
 * changed. A caller that reapplies the CRT scanline/vignette texture (a
 * read-modify-write dim pass) MUST scope it to exactly these rects, never
 * the whole meter rect -- reapplying to untouched pixels darkens them
 * further every single tick with no repaint to undo it. */
void orion_ui_meter_delta(sgfx_device_t* d, orion_ui_rect_t r, int value_pct, int prev_pct,
                          sgfx_rgba8_t accent, sgfx_rgba8_t bg,
                          orion_ui_rect_t* out_seg_rect, orion_ui_rect_t* out_text_rect);
void orion_ui_selector(sgfx_device_t* d, orion_ui_rect_t r,
                       const char* left, const char* center, const char* right,
                       int selected_idx, int disabled);
/* Matches orion_ui_selector()'s own equal-thirds segment math exactly, same
 * reasoning as orion_ui_toggle_hit(). Returns 0/1/2, or -1 if (x,y) isn't
 * within r at all. */
int orion_ui_selector_hit(orion_ui_rect_t r, int x, int y);
void orion_ui_badge(sgfx_device_t* d, orion_ui_rect_t r, const char* text,
                    sgfx_rgba8_t color, int emphatic);
void orion_ui_separator(sgfx_device_t* d, int x, int y, int w,
                        const char* label, int style);
void orion_ui_ornament(sgfx_device_t* d, orion_ui_ornament_t kind,
                       orion_ui_rect_t r, sgfx_rgba8_t color);

/* 5x7 helpers. Width measurement is exact for the actual renderer. */
int  orion_ui_text_width(const char* s, int scale);
void orion_ui_text(sgfx_device_t* d, int x, int y, const char* s,
                   sgfx_rgba8_t color, int scale);
void orion_ui_text_fit(sgfx_device_t* d, int x, int y, int max_w,
                       const char* s, sgfx_rgba8_t color, int scale);

/* Carousel/marquee text: for text that
 * doesn't fit its box, an alternative to orion_ui_text_fit()'s "..."
 * truncation -- pauses at the start, scrolls left until the tail clears
 * the box, pauses at the end, then wraps. Text that already fits just
 * draws statically (identical to orion_ui_text()), so it's a safe drop-in
 * anywhere text_fit is used today.
 *
 * `now_ms` is a caller-supplied clock rather than this file reading one
 * itself: ui_components.c/ui_theme.c/ui_screens.c are deliberately free of
 * any platform/hardware include (tools/ui_preview.c links them standalone
 * on a host with no orion_millis()), and every other piece of "live" state
 * these draw functions need (model structs, current backlight %, etc.) is
 * already passed in the same way rather than pulled from a global. Callers
 * on real firmware pass orion_millis(); the host preview can pass any
 * fixed or swept value to render a specific animation phase.
 *
 * Because this needs periodic redraws to actually animate (unlike every
 * other draw call here, which only needs to run again when something
 * changed), it clips to exactly `r` via SGFX's real clip rect -- a caller
 * redrawing this every poll tick only ever touches this one small rect,
 * not a wider area, so it fits the same "redraw only the dirty region"
 * discipline as everything else rather than reintroducing flicker. */
void orion_ui_text_marquee(sgfx_device_t* d, orion_ui_rect_t r, const char* s,
                           sgfx_rgba8_t fg, sgfx_rgba8_t bg, int scale, uint32_t now_ms);

/* Redraws every currently-registered carousel (any orion_ui_text_marquee()
 * call whose text didn't fit its box, since the last real orion_ui_clear())
 * at the marquee clock's current position. Each entry stores the EXACT
 * rect/text/colors/scale its real draw call used -- not a separately
 * hand-maintained guess -- so this can never touch a rect that doesn't
 * match a real widget currently on screen. Caller should throttle how
 * often this runs (e.g. ~80-100ms -- see touchmenu.c's call sites) rather
 * than calling it every poll tick; no-op (and skips the sgfx_present()) if
 * nothing on the current screen is actually overflowing. */
void orion_ui_marquee_tick(sgfx_device_t* d);

/* For a caller about to redraw ONE region more than once per real content
 * change (a scroll drag, a partial repaint) rather than the whole screen:
 * discards only the registered entries that overlap `region`, so the next
 * redraw of that region re-registers a fresh, accurate set without
 * accumulating stale copies at old (e.g. pre-scroll) positions, while
 * leaving other regions' entries (chrome outside what's being redrawn)
 * alone. Call this immediately before redrawing `region`'s content. */
void orion_ui_marquee_reset_region(orion_ui_rect_t region);

/* True delta-redraw's marquee counterpart: for a caller that
 * just shifted an already-rendered region by `dy` LOGICAL pixels (see
 * touchmenu.c's orion_ui_scroll_delta_shift()) rather than redrawing it --
 * moves (instead of dropping) every registered entry that overlapped
 * `region` before the shift, so orion_ui_marquee_tick() keeps animating a
 * card's overflow text at its new, already-correct-post-shift position
 * instead of either a stale one (ghosting -- the bug orion_ui_marquee_
 * reset_region() above was built for) or none at all (freezing -- the bug
 * found when reset_region() was first (over-)applied here instead of this
 * function). Also re-anchors the moved entry's clip to `region` itself
 * (the caller's full scrollable viewport) rather than whatever narrower
 * transient band clip was active when it was first drawn, since that
 * viewport -- not any one redraw pass's band -- is the real, unchanging
 * boundary the text must never spill outside of. */
void orion_ui_marquee_shift_region(orion_ui_rect_t region, int dy);

/* Debug/introspection only, for the `screenshot` console command: the
 * registry is real, live "what's currently drawn where" ground truth (see
 * orion_ui_text_marquee()'s own comment), so exposing it
 * read-only is a cheap way to get real overlap-check data alongside a
 * framebuffer dump, instead of guessing coordinates by eye. */
int orion_ui_marquee_debug_count(void);
void orion_ui_marquee_debug_get(int i, orion_ui_rect_t* out_r, char* out_text, size_t text_sz);

/* Draw-time clock for every OTHER widget below (button/badge/toggle/tab/
 * selector/meter/separator/missing_asset/topbar) that carousels its own
 * text internally -- a setter/getter pair instead of threading a `now_ms`
 * parameter through all of their signatures (and therefore through every
 * one of their many call sites app-wide), the same "draw-time state set by
 * the app layer, read internally" shape already used by
 * orion_ui_topbar_set_power() above. The app layer calls
 * orion_ui_set_time_ms(orion_millis()) once per redraw pass; the host
 * preview tool can call it with any fixed or swept value since this file
 * still never reads a clock itself. Defaults to 0 (static text, matches
 * orion_ui_text_marquee()'s own phase-0 = start-of-cycle behavior) if
 * never called. */
void orion_ui_set_time_ms(uint32_t now_ms);
uint32_t orion_ui_get_time_ms(void);

#ifdef __cplusplus
}
#endif
