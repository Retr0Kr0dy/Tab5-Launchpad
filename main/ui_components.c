#include "ui_components.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

static sgfx_rgba8_t darken(sgfx_rgba8_t c, int delta)
{
    int r = c.r - delta, g = c.g - delta, b = c.b - delta;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    return (sgfx_rgba8_t){ (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 };
}

static struct {
    int present;
    int percent;
    int charging;
} s_topbar_power = { 0, 0, 0 };

static struct {
    int enabled;
    int pct;
} s_topbar_mem = { 0, 0 };

static uint32_t s_now_ms = 0;
void orion_ui_set_time_ms(uint32_t now_ms) { s_now_ms = now_ms; }
uint32_t orion_ui_get_time_ms(void) { return s_now_ms; }

/* Forward declaration -- real definition lives with the other small pixel
 * primitives further down; marquee_draw() (below) needs it earlier. */
static void fill(sgfx_device_t* d, int x, int y, int w, int h, sgfx_rgba8_t c);

/* The configured column count is literal and orientation-independent --
 * "1" always means exactly one column, in portrait or landscape, rather
 * than transposing into a locked row count in portrait (which inverts the
 * intent at the extremes: a deliberate single-column layout would
 * otherwise become a single ROW, squeezing every item into narrow
 * columns). The min-cell-width floor is a safety net for the opposite
 * case (a large column count requested on a narrow orientation),
 * auto-reducing -- never increasing -- columns so cells never get
 * unreadably cramped. */

/* Shared by orion_ui_grid_rect() and orion_ui_grid_max_scroll() below so
 * the two can never derive a different column/row count from the same
 * inputs -- exactly the "one shared computation feeds both the draw call
 * and whatever depends on its geometry" discipline this codebase already
 * applies everywhere else (terminal_view_rect(), settings_*_btn(), etc). */
static void grid_layout(orion_ui_rect_t area, int count, int gap, int max_cell_h,
                        int* out_cols, int* out_rows, int* out_cw, int* out_ch)
{
    if (count < 1) count = 1;
    int cols = orion_ui_get_grid_cols();
    if (cols < 1) cols = 1;
    int min_cell_w = orion_ui_theme()->style.grid_min_cell_w;
    if (min_cell_w < 120) min_cell_w = 120;
    while (cols > 1 && (area.w - (cols - 1) * gap) / cols < min_cell_w)
        --cols;
    int rows = (count + cols - 1) / cols;
    if (rows < 1) rows = 1;
    *out_cols = cols;
    *out_rows = rows;
    *out_cw = (area.w - (cols - 1) * gap) / cols;
    /* Cells don't stretch to occupy the full screen just because there's
     * room -- a handful of items should look like a handful of
     * natural-sized items, not be inflated to fill whatever space happens
     * to be available. Using min(evenly-divided, max_cell_h) here would
     * silently allow a SHRINK below the cap when more rows exist than fit
     * -- cells getting squished instead of the screen offering a
     * scrollbar. Fixed at max_cell_h whenever a cap is given; overflow
     * becomes real
     * (orion_ui_grid_max_scroll() below), which is what a caller doing
     * drag-to-scroll needs. */
    *out_ch = max_cell_h > 0 ? max_cell_h : (area.h - (rows - 1) * gap) / rows;
}

orion_ui_rect_t orion_ui_grid_rect(orion_ui_rect_t area, int count,
                                   int gap, int max_cell_h, int index, int scroll_px)
{
    int cols, rows, cw, ch;
    grid_layout(area, count, gap, max_cell_h, &cols, &rows, &cw, &ch);
    int col = index % cols;
    int row = index / cols;
    return (orion_ui_rect_t){
        area.x + col * (cw + gap),
        area.y + row * (ch + gap) - scroll_px,
        cw, ch
    };
}

int orion_ui_grid_max_scroll(orion_ui_rect_t area, int count, int gap, int max_cell_h)
{
    int cols, rows, cw, ch;
    grid_layout(area, count, gap, max_cell_h, &cols, &rows, &cw, &ch);
    (void)cols; (void)cw;
    int total_h = rows * (ch + gap) - gap;
    int max_scroll = total_h - area.h;
    return max_scroll > 0 ? max_scroll : 0;
}

int orion_ui_text_width(const char* s, int scale)
{
    if (!s || scale <= 0) return 0;
    return (int)strlen(s) * 6 * scale;
}

static sgfx_rgba8_t mix_color(sgfx_rgba8_t a, sgfx_rgba8_t b, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (sgfx_rgba8_t){
        (uint8_t)((a.r * (100 - pct) + b.r * pct) / 100),
        (uint8_t)((a.g * (100 - pct) + b.g * pct) / 100),
        (uint8_t)((a.b * (100 - pct) + b.b * pct) / 100),
        255
    };
}

void orion_ui_text(sgfx_device_t* d, int x, int y, const char* s,
                   sgfx_rgba8_t color, int scale)
{
    if (!d || !s || scale <= 0) return;
    const orion_ui_theme_t* t = orion_ui_theme();
    if (t->effects.overlay == ORION_UI_OVERLAY_CRT &&
        t->effects.phosphor_glow && t->effects.glow_strength > 0 &&
        t->effects.glow_radius > 0) {
        int r = t->effects.glow_radius;
        /* A cheap phosphor halo: four dim offset glyph masks, then the crisp
         * foreground. This stays local to text pixels and avoids a full-screen
         * blur pass or another framebuffer. */
        sgfx_rgba8_t halo = mix_color(darken(t->bg, 0), t->effects.glow_color,
                                      t->effects.glow_strength);
        halo = mix_color(halo, color, t->effects.glow_strength / 3);
        sgfx_text5x7_scaled(d, x-r, y,   s, halo, scale, scale);
        sgfx_text5x7_scaled(d, x+r, y,   s, halo, scale, scale);
        sgfx_text5x7_scaled(d, x,   y-r, s, halo, scale, scale);
        sgfx_text5x7_scaled(d, x,   y+r, s, halo, scale, scale);
    }
    sgfx_text5x7_scaled(d, x, y, s, color, scale, scale);
}

void orion_ui_text_fit(sgfx_device_t* d, int x, int y, int max_w,
                       const char* s, sgfx_rgba8_t color, int scale)
{
    if (!d || !s || max_w <= 0 || scale <= 0) return;
    int max_chars = max_w / (6 * scale);
    if (max_chars <= 0) return;
    size_t len = strlen(s);
    if ((int)len <= max_chars) {
        orion_ui_text(d, x, y, s, color, scale);
        return;
    }
    char buf[128];
    int n = imin(max_chars, (int)sizeof(buf) - 1);
    if (n <= 3) return;
    memcpy(buf, s, (size_t)(n - 3));
    memcpy(buf + n - 3, "...", 3);
    buf[n] = '\0';
    orion_ui_text(d, x, y, buf, color, scale);
}

/* Always paints its own background (`bg`) before drawing a new scroll
 * offset, using the SAME rect for both the clearing fill and the text
 * draw/clip -- relying on the caller to have already cleared this exact
 * rect leaves the previous frame's glyph strokes wherever the new frame's
 * glyphs don't happen to overwrite them (ghosting/smearing) the moment
 * this is redrawn to animate rather than only on a full widget repaint. */
#define ORION_UI_MARQUEE_MAX 40
typedef struct {
    orion_ui_rect_t r;
    char text[80];
    sgfx_rgba8_t fg, bg;
    int scale;
    sgfx_rect_t clip;
} orion_ui_marquee_slot_t;
static orion_ui_marquee_slot_t s_marquee[ORION_UI_MARQUEE_MAX];
static int s_marquee_count = 0;

static void marquee_draw(sgfx_device_t* d, orion_ui_rect_t r, const char* s,
                         sgfx_rgba8_t fg, sgfx_rgba8_t bg, int scale, uint32_t now_ms)
{
    int glyph_h = 7*scale + 2;
    fill(d, r.x, r.y, r.w, glyph_h, bg);

    int text_w = orion_ui_text_width(s, scale);
    if (text_w <= r.w) {
        orion_ui_text(d, r.x, r.y, s, fg, scale);
        return;
    }

    const int pause_ms = 900;
    int scroll_px = text_w - r.w;
    /* ~60px/s: readable without feeling sluggish on typical filename
     * lengths; duration derives from distance so a much longer name
     * doesn't fly past unreadably fast just because it's longer. */
    uint32_t scroll_ms = (uint32_t)scroll_px * 1000u / 60u;
    if (scroll_ms < 1) scroll_ms = 1;
    uint32_t cycle = (uint32_t)pause_ms * 2u + scroll_ms * 2u;
    uint32_t phase = now_ms % cycle;

    int offset;
    if (phase < (uint32_t)pause_ms) {
        offset = 0;
    } else if (phase < (uint32_t)pause_ms + scroll_ms) {
        offset = (int)((phase - (uint32_t)pause_ms) * (uint32_t)scroll_px / scroll_ms);
    } else if (phase < (uint32_t)pause_ms * 2u + scroll_ms) {
        offset = scroll_px;
    } else {
        uint32_t back = phase - ((uint32_t)pause_ms * 2u + scroll_ms);
        offset = scroll_px - (int)(back * (uint32_t)scroll_px / scroll_ms);
    }

    /* sgfx_reset_clip() resets the device's clip to the full screen, not
     * to whatever clip the caller had active before this function ran --
     * using it unconditionally here would clobber an outer, already-active
     * clip (e.g. Diagnostics' content-band redraw) back to full-screen the
     * moment any one button's text overflowed, letting later draws in the
     * same pass paint over chrome outside that band with nothing to clean
     * it up afterward. The clip used DURING the scrolling draw must be `r`
     * intersected with the clip active on entry, not `r` verbatim and not
     * a hard reset -- either of those can let scrolled glyph pixels land
     * outside the caller's real, active boundary, permanently, since
     * nothing outside the content band ever redraws to clean it up. */
    sgfx_rect_t prev_clip = d->clip;
    int cx0 = imax(r.x, prev_clip.x);
    int cy0 = imax(r.y, prev_clip.y);
    int cx1 = imin(r.x + r.w,   prev_clip.x + prev_clip.w);
    int cy1 = imin(r.y + glyph_h, prev_clip.y + prev_clip.h);
    sgfx_rect_t clip = {
        (int16_t)cx0, (int16_t)cy0,
        (int16_t)(cx1 > cx0 ? cx1 - cx0 : 0),
        (int16_t)(cy1 > cy0 ? cy1 - cy0 : 0)
    };
    sgfx_set_clip(d, clip);
    orion_ui_text(d, r.x - offset, r.y, s, fg, scale);
    sgfx_set_clip(d, prev_clip);
}

void orion_ui_text_marquee(sgfx_device_t* d, orion_ui_rect_t r, const char* s,
                           sgfx_rgba8_t fg, sgfx_rgba8_t bg, int scale, uint32_t now_ms)
{
    if (!d || !s || scale <= 0 || r.w <= 0) return;
    marquee_draw(d, r, s, fg, bg, scale, now_ms);

    /* Register for orion_ui_marquee_tick() only when this text actually
     * overflows (fits-and-static text needs no periodic redraw). The
     * registry is reset by orion_ui_clear() -- called at the start of
     * every real full-screen redraw, never during a tick-only pass -- so
     * it always holds exactly what the CURRENT screen most recently drew,
     * with no separate staleness bookkeeping: a screen switch naturally
     * discards the previous screen's entries the moment it does its own
     * first real redraw. This is what makes the tick's redraw rect always
     * match a real, current draw call instead of a guessed/stale one. */
    if (orion_ui_text_width(s, scale) <= r.w) return;

    /* Registration is tied to the SAME clip that already governs real
     * visibility: if `r` isn't actually within the active d->clip, it
     * isn't really on screen right now, and ticking it later would only
     * ever be wrong. Callers like draw_action_grid() (Diagnostics,
     * touchmenu.c) draw every row every frame with no visibility check of
     * their own -- they rely entirely on sgfx_set_clip() to visually hide
     * rows scrolled above the content band -- so without this check here,
     * a row scrolled out of view would still register its true position,
     * leaking a permanently-redrawn, unclipped ghost entry into the
     * chrome/topbar zone above the content band. */
    {
        int glyph_h = 7*scale + 2;
        if (r.x + r.w <= d->clip.x || r.x >= d->clip.x + d->clip.w ||
            r.y + glyph_h <= d->clip.y || r.y >= d->clip.y + d->clip.h) {
            return;
        }
    }

    /* A blind append would add a fresh duplicate entry every time a screen
     * whose own poll loop redraws the SAME stationary widget repeatedly
     * (e.g. the backlight meter during a drag, once per percent change)
     * ticks, quickly burning through ORION_UI_MARQUEE_MAX on one widget
     * and starving out other, legitimately-overflowing text on the same
     * screen. An entry at the same (x,y) is the same on-screen widget
     * redrawing itself, not a new one -- update it in place instead. */
    for (int i = 0; i < s_marquee_count; ++i) {
        if (s_marquee[i].r.x == r.x && s_marquee[i].r.y == r.y) {
            s_marquee[i].r = r; s_marquee[i].fg = fg; s_marquee[i].bg = bg; s_marquee[i].scale = scale;
            s_marquee[i].clip = d->clip;
            snprintf(s_marquee[i].text, sizeof s_marquee[i].text, "%s", s);
            return;
        }
    }
    if (s_marquee_count < ORION_UI_MARQUEE_MAX) {
        orion_ui_marquee_slot_t* m = &s_marquee[s_marquee_count++];
        m->r = r; m->fg = fg; m->bg = bg; m->scale = scale;
        m->clip = d->clip;
        snprintf(m->text, sizeof m->text, "%s", s);
    }
}

/* Each registry entry remembers the clip that was ACTUALLY active when it
 * registered (orion_ui_text_marquee() stores d->clip into the slot) and
 * re-applies that stored clip -- not whatever's globally active -- around
 * every tick redraw of that entry. Registering an entry only when it
 * overlaps the clip active at registration time stops a fully-off-screen
 * row from leaking, but does nothing for a row that's only PARTIALLY
 * visible (straddling a content band's own top/bottom edge, correctly
 * clipped during its real draw): ticks fire from the screen's own poll
 * loop, entirely outside whatever brief clip-narrowed scope the original
 * draw call used, so redrawing with "whatever clip is active at tick
 * time" would draw that same row with no effective boundary at all on the
 * very next animation frame. */
void orion_ui_marquee_tick(sgfx_device_t* d)
{
    if (!d || s_marquee_count == 0) return;
    orion_ui_effects_prepare_draw(d);
    sgfx_rect_t outer = d->clip;
    for (int i = 0; i < s_marquee_count; ++i) {
        orion_ui_marquee_slot_t* m = &s_marquee[i];
        sgfx_set_clip(d, m->clip);
        marquee_draw(d, m->r, m->text, m->fg, m->bg, m->scale, s_now_ms);
    }
    sgfx_set_clip(d, outer);
    sgfx_present(d);
}

/* For screens that redraw a REGION (not the whole screen) more than once
 * per real state change -- e.g. the Diagnostics scroll drag, which
 * redraws its whole button column on every pixel of movement.
 * Registration only APPENDS, so without discarding stale entries here,
 * each of those redraws would add a fresh copy of every visible button's
 * overflow text at its (moving) rect, while the earlier copies -- now at
 * stale, scrolled-past positions -- would stay registered and get
 * dutifully redrawn as ghost text until the next full orion_ui_clear().
 * Discards only entries that actually overlap `region`, so a caller
 * redrawing one region (like
 * the content band below a topbar) doesn't discard another region's
 * legitimate entries (like the topbar's own scrolling title) that this
 * particular redraw pass never touched. */
void orion_ui_marquee_reset_region(orion_ui_rect_t region)
{
    int w = 0;
    for (int i = 0; i < s_marquee_count; ++i) {
        orion_ui_marquee_slot_t* m = &s_marquee[i];
        int glyph_h = 7*m->scale + 2;
        int overlaps = !(m->r.x + m->r.w <= region.x || m->r.x >= region.x + region.w ||
                          m->r.y + glyph_h <= region.y || m->r.y >= region.y + region.h);
        if (!overlaps) s_marquee[w++] = *m;
    }
    s_marquee_count = w;
}

void orion_ui_marquee_shift_region(orion_ui_rect_t region, int dy)
{
    if (dy == 0) return;
    for (int i = 0; i < s_marquee_count; ++i) {
        orion_ui_marquee_slot_t* m = &s_marquee[i];
        int glyph_h = 7*m->scale + 2;
        int overlaps = !(m->r.x + m->r.w <= region.x || m->r.x >= region.x + region.w ||
                          m->r.y + glyph_h <= region.y || m->r.y >= region.y + region.h);
        if (!overlaps) continue;
        m->r.y += dy;
        m->clip = (sgfx_rect_t){ (int16_t)region.x, (int16_t)region.y,
                                 (int16_t)region.w, (int16_t)region.h };
    }
}

int orion_ui_marquee_debug_count(void) { return s_marquee_count; }

void orion_ui_marquee_debug_get(int i, orion_ui_rect_t* out_r, char* out_text, size_t text_sz)
{
    if (i < 0 || i >= s_marquee_count) return;
    if (out_r) *out_r = s_marquee[i].r;
    if (out_text && text_sz) snprintf(out_text, text_sz, "%s", s_marquee[i].text);
}

static void px(sgfx_device_t* d, int x, int y, sgfx_rgba8_t c)
{
    sgfx_draw_pixel(d, x, y, c);
}
static void hline(sgfx_device_t* d, int x, int y, int w, sgfx_rgba8_t c)
{
    if (w > 0) sgfx_draw_fast_hline(d, x, y, w, c);
}
static void vline(sgfx_device_t* d, int x, int y, int h, sgfx_rgba8_t c)
{
    if (h > 0) sgfx_draw_fast_vline(d, x, y, h, c);
}
static void fill(sgfx_device_t* d, int x, int y, int w, int h, sgfx_rgba8_t c)
{
    if (w > 0 && h > 0) sgfx_fill_rect(d, x, y, w, h, c);
}
static void rect(sgfx_device_t* d, int x, int y, int w, int h, sgfx_rgba8_t c)
{
    if (w > 1 && h > 1) sgfx_draw_rect(d, x, y, w, h, c);
}
static void rect_thick(sgfx_device_t* d, int x, int y, int w, int h, int th, sgfx_rgba8_t c)
{
    for (int i = 0; i < th; ++i) rect(d, x + i, y + i, w - 2*i, h - 2*i, c);
}
static void line(sgfx_device_t* d, int x0, int y0, int x1, int y1, sgfx_rgba8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        px(d, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void line_thick(sgfx_device_t* d, int x0, int y0, int x1, int y1, int th, sgfx_rgba8_t c)
{
    int half = th / 2;
    if ((y0 > y1 ? y0 - y1 : y1 - y0) > (x0 > x1 ? x0 - x1 : x1 - x0)) {
        for (int i = -half; i <= half; ++i) line(d, x0 + i, y0, x1 + i, y1, c);
    } else {
        for (int i = -half; i <= half; ++i) line(d, x0, y0 + i, x1, y1 + i, c);
    }
}
static void diamond(sgfx_device_t* d, int cx, int cy, int rx, int ry, sgfx_rgba8_t c, int filled)
{
    if (filled) {
        for (int y = -ry; y <= ry; ++y) {
            int span = rx - (rx * (y < 0 ? -y : y)) / imax(1, ry);
            hline(d, cx - span, cy + y, span * 2 + 1, c);
        }
    } else {
        line(d, cx, cy - ry, cx + rx, cy, c);
        line(d, cx + rx, cy, cx, cy + ry, c);
        line(d, cx, cy + ry, cx - rx, cy, c);
        line(d, cx - rx, cy, cx, cy - ry, c);
    }
}

static int vault_deco_enabled(void)
{
    return orion_ui_theme()->style.chrome_style == ORION_UI_CHROME_VAULT_DECO;
}

static void draw_rivet(sgfx_device_t* d, int cx, int cy, sgfx_rgba8_t edge, sgfx_rgba8_t hi)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    int rs = t->style.rivet_size;
    if (rs <= 0) return;
    int dsz = rs * 2 + 1;
    fill(d, cx - rs, cy - rs, dsz, dsz, edge);
    if (rs >= 2) fill(d, cx - rs + 1, cy - rs + 1, rs, rs, hi);
}

static void draw_corner_marks(sgfx_device_t* d, orion_ui_rect_t r, sgfx_rgba8_t c)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    int cut = t->style.corner_cut;
    if (cut <= 0 || r.w < cut * 3 || r.h < cut * 3) return;
    int inset = imax(2, t->metrics.border_w + 1);
    line(d, r.x + inset, r.y + cut, r.x + cut, r.y + inset, c);
    line(d, r.x + r.w - cut - 1, r.y + inset, r.x + r.w - inset - 1, r.y + cut, c);
    line(d, r.x + inset, r.y + r.h - cut - 1, r.x + cut, r.y + r.h - inset - 1, c);
    line(d, r.x + r.w - cut - 1, r.y + r.h - inset - 1, r.x + r.w - inset - 1, r.y + r.h - cut - 1, c);
}

static void draw_hazard_band(sgfx_device_t* d, int x, int y, int w, int h,
                             sgfx_rgba8_t a, sgfx_rgba8_t b)
{
    if (w <= 0 || h <= 0) return;
    fill(d, x, y, w, h, b);
    const int step = 12;
    for (int sx = x - h; sx < x + w; sx += step) {
        for (int yy = 0; yy < h; ++yy) {
            int xx = sx + yy;
            if (xx >= x && xx < x + w) fill(d, xx, y + yy, 4, 1, a);
        }
    }
}

static void draw_deco_background(sgfx_device_t* d)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    int w = d->caps.width, h = d->caps.height;
    int pitch = t->style.grid_pitch > 0 ? t->style.grid_pitch : 24;
    sgfx_rgba8_t grid = darken(t->edge, 34);
    if (t->style.background_pattern == ORION_UI_BG_GRID) {
        for (int x = pitch; x < w; x += pitch) vline(d, x, 0, h, grid);
        for (int y = pitch; y < h; y += pitch) hline(d, 0, y, w, grid);
    } else if (t->style.background_pattern == ORION_UI_BG_DOTS) {
        for (int y = pitch / 2; y < h; y += pitch)
            for (int x = pitch / 2; x < w; x += pitch)
                fill(d, x, y, 2, 2, grid);
    }
    if (vault_deco_enabled() && t->style.ornament_density > 0) {
        int rail = imax(10, t->metrics.margin / 2);
        fill(d, 0, 0, rail, h, darken(t->panel, 8));
        fill(d, w - rail, 0, rail, h, darken(t->panel, 8));
        for (int y = t->metrics.topbar_h + 12; y < h - 20; y += 56) {
            diamond(d, rail / 2, y, 4, 4, t->edge_hi, 0);
            diamond(d, w - rail / 2 - 1, y, 4, 4, t->edge_hi, 0);
        }
    }
}

void orion_ui_clear(sgfx_device_t* d)
{
    /* A full redraw overwrites every pixel that a previous dynamic CRT band
     * may have touched, so its saved strip must be invalidated before the
     * new frame is built. */
    orion_ui_effects_reset();
    sgfx_clear(d, orion_ui_theme()->bg);
    draw_deco_background(d);
    /* Every real full-screen redraw starts here -- resetting the marquee
     * registry at this single choke point (not scattered per-screen) is
     * what guarantees orion_ui_marquee_tick() never redraws a rect left
     * over from whatever screen was on-screen before this one. */
    s_marquee_count = 0;
}

void orion_ui_topbar_set_power(int percent, int charging, int present)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_topbar_power.present = present ? 1 : 0;
    s_topbar_power.percent = percent;
    s_topbar_power.charging = charging ? 1 : 0;
}

void orion_ui_topbar_set_power_unknown(void)
{
    s_topbar_power.present = 0;
    s_topbar_power.percent = 0;
    s_topbar_power.charging = 0;
}

/* orion_ui_topbar() positions right_status against this real, computed
 * power-group width rather than a hardcoded guess, since the group's true
 * width grows with UI-size scale -- a fixed constant can undersize it and
 * let right_status collide into it at larger scales. Shared here so both
 * functions use the exact same number instead of one guessing at the
 * other's layout. Always sized for the CHARGING case (the widest state)
 * regardless of whether it's actually charging right now, so
 * right_status's boundary doesn't shift depending on charging state -- a
 * stable reservation, not a tightest-possible one. */
/* CHG_ICON_W: fixed-size lightning-bolt glyph replacing "CHG" text -- a
 * fixed-size icon needs no text-metrics reservation and can't ever
 * carousel, unlike the text it replaces. */
#define CHG_ICON_W 22

static int topbar_power_group_width(void)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    int pct_w = orion_ui_text_width("100%", t->metrics.small_scale);
    const int icon_w = 46, gap = 6;
    return pct_w + gap + icon_w + gap + CHG_ICON_W;
}

/* RAM-usage badge -- same real/optional-width
 * shape as the battery group above (sized for worst case "100%" so it can't
 * carousel or shift width as the number changes), sitting one gap further
 * left. Returns 0 when disabled so right_status's floor and the badge's own
 * x position both collapse back to "as if this badge didn't exist" with no
 * separate disabled-width case to keep in sync. */
static int topbar_mem_group_width(void)
{
    if (!s_topbar_mem.enabled) return 0;
    const orion_ui_theme_t* t = orion_ui_theme();
    int pct_w = orion_ui_text_width("100%", t->metrics.small_scale);
    const int icon_w = 24, gap = 6;
    return icon_w + gap + pct_w;
}

/* Disabled is a true no-op: zero reserved width (topbar_mem_group_width()
 * returns 0), nothing drawn, nothing to clear -- the enabled/disabled
 * transition itself is only ever driven by a real Settings-tab toggle tap,
 * which already goes through a full topbar redraw (orion_ui_clear() first),
 * so there's no stale-pixel case this needs to guard against the way
 * topbar_power() must (that badge is always present, just sometimes
 * "unknown"; this one can genuinely not exist in the layout at all). */
static void topbar_mem(sgfx_device_t* d, int w)
{
    if (!s_topbar_mem.enabled) return;
    const orion_ui_theme_t* t = orion_ui_theme();
    int box_w = topbar_mem_group_width();
    int x = w - t->metrics.margin - topbar_power_group_width() - 10 - box_w;
    /* Shares the same mid = h/2 reference as topbar_power() and
     * orion_ui_topbar()'s own path/right_status text, each centering its
     * own (icon or text) height around it, so everything sits on one real
     * line regardless of topbar_h's UI-size scaling -- independent,
     * hand-picked y offsets per element drift out of alignment as soon as
     * their heights differ. */
    int mid = t->metrics.topbar_h / 2;
    const int icon_sz = 20;
    int text_h = 7 * t->metrics.small_scale;
    int icon_y = mid - icon_sz / 2;
    int text_y = mid - text_h / 2;
    fill(d, x, mid - 14, box_w, 28, t->panel);
    sgfx_rgba8_t level = s_topbar_mem.pct >= 85 ? t->danger :
                         (s_topbar_mem.pct >= 65 ? t->warning : t->good);
    orion_ui_icon(d, ORION_ICON_HARDWARE, x, icon_y, icon_sz, level);
    char pct[12];
    snprintf(pct, sizeof pct, "%d%%", s_topbar_mem.pct);
    orion_ui_text_marquee(d, (orion_ui_rect_t){x + icon_sz + 6, text_y, box_w - icon_sz - 6, 0},
                          pct, level, t->panel, t->metrics.small_scale, s_now_ms);
}

void orion_ui_topbar_set_mem(int enabled, int used_pct)
{
    s_topbar_mem.enabled = enabled ? 1 : 0;
    if (used_pct < 0) used_pct = 0;
    if (used_pct > 100) used_pct = 100;
    s_topbar_mem.pct = used_pct;
}

void orion_ui_topbar_mem_only(sgfx_device_t* d, int w)
{
    orion_ui_effects_prepare_draw(d);
    topbar_mem(d, w);
}

/* Always clears the bolt's own box first, unconditionally, then
 * conditionally draws the bolt on top -- same "clear-then-conditionally-
 * draw inside one self-contained region" shape marquee_draw()'s own bg
 * fill uses. This function is called both as part of a full screen redraw
 * (where a preceding orion_ui_clear() already erased the previous frame)
 * and from a periodic partial refresh with no full clear first
 * (orion_ui_topbar_power_only()) -- without the unconditional clear here,
 * a charging->not-charging transition on that second path would leave the
 * bolt glyph stuck on screen forever, since nothing else ever clears its
 * box. */
static void topbar_power(sgfx_device_t* d, int w)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    /* mid = h/2, the same reference topbar_mem() and orion_ui_topbar()'s
     * own path/right_status text derive their y from, so every topbar
     * element sits on one real line instead of independently hand-picked
     * offsets drifting apart. */
    int mid = t->metrics.topbar_h / 2;
    /* Sized from the real worst case ("100%") at the CURRENT scale rather
     * than a fixed pixel width tuned for one scale step -- a fixed width
     * can't grow with UI-size scale and would needlessly trigger the
     * carousel for content that's short, fixed-format, and should always
     * fit. */
    int pct_w = orion_ui_text_width("100%", t->metrics.small_scale);
    const int icon_w = 46, icon_h = 24;
    const int gap = 8; /* real breathing room around the icon */

    if (!s_topbar_power.present) {
        int slot_w = pct_w + gap + icon_w;
        int x = w - t->metrics.margin - slot_w;
        orion_ui_badge(d, (orion_ui_rect_t){x, mid - 14, slot_w, 28}, "BAT ?", t->dim, 0);
        return;
    }

    sgfx_rgba8_t level = s_topbar_power.percent < 20 ? t->danger :
                         (s_topbar_power.percent < 45 ? t->warning : t->good);
    char pct[12];
    snprintf(pct, sizeof pct, "%d%%", s_topbar_power.percent);
    /* Anchored off the SAME stable (charging-sized) width orion_ui_topbar()
     * reserves for right_status, not a tighter one that would shrink when
     * not charging -- keeps the icon's own position constant regardless of
     * charging state, so it doesn't visually jump left/right every time
     * charging toggles. */
    int total_w = topbar_power_group_width();
    int x = w - t->metrics.margin - total_w;

    int text_h = 7 * t->metrics.small_scale;
    orion_ui_text_marquee(d, (orion_ui_rect_t){x, mid - text_h / 2, pct_w, 0}, pct, level, t->panel, t->metrics.small_scale, s_now_ms);
    int bx = x + pct_w + gap, by = mid - icon_h / 2, bw = icon_w, bh = icon_h;
    fill(d, bx, by, bw, bh, darken(t->panel_hi, 10));
    rect_thick(d, bx, by, bw, bh, 2, t->edge_hi);
    fill(d, bx + bw, by + 7, 6, 10, t->edge_hi);
    int inner_w = bw - 8;
    fill(d, bx + 4, by + 4, inner_w, bh - 8, darken(t->panel, 8));
    fill(d, bx + 4, by + 4, inner_w * s_topbar_power.percent / 100, bh - 8, level);

    /* Standalone lightning-bolt glyph, replacing the old "CHG" text --
     * fixed-size icon, real gap after the battery icon, can't carousel.
     * Box cleared unconditionally first (see function header comment). */
    int cx = bx + bw + gap, cy = by, cw = CHG_ICON_W, ch = bh;
    fill(d, cx, cy, cw, ch, t->panel);
    if (s_topbar_power.charging) {
        int ox = cx + 2;
        line_thick(d, ox + 8, cy + 2,  ox + 2, cy + 12, 2, t->accent_hi);
        line_thick(d, ox + 2, cy + 12, ox + 7, cy + 12, 2, t->accent_hi);
        line_thick(d, ox + 7, cy + 12, ox + 1, cy + 22, 2, t->accent_hi);
    }
}

void orion_ui_topbar(sgfx_device_t* d, int w, const char* path,
                     const char* right_status)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    int h = t->metrics.topbar_h;
    fill(d, 0, 0, w, h, t->panel);
    fill(d, 0, h - 1, w, 1, t->edge);
    fill(d, 0, 0, 16, h, t->edge);
    fill(d, w - 16, 0, 16, h, t->edge);
    int path_max_w = w / 2;
    const char* path_s = path ? path : "ORION";
    sgfx_rgba8_t path_bg = t->panel;
    if (vault_deco_enabled()) {
        hline(d, 18, 5, w - 36, t->edge_hi);
        hline(d, 18, h - 8, w - 36, t->edge_hi);
        if (t->style.hazard_stripes)
            draw_hazard_band(d, 18, h - 7, w - 36, 5, t->edge_hi, darken(t->panel_hi, 8));
        int plate_x = t->metrics.margin - 8;
        int plate_y = 10;
        int plate_w = path_max_w + 16;
        int plate_h = imax(34, h - 20);
        fill(d, plate_x, plate_y, plate_w, plate_h, t->panel_hi);
        rect_thick(d, plate_x, plate_y, plate_w, plate_h, 2, t->edge_hi);
        draw_corner_marks(d, (orion_ui_rect_t){plate_x, plate_y, plate_w, plate_h}, t->accent);
        draw_rivet(d, plate_x + 7, plate_y + 7, t->edge, t->accent_hi);
        draw_rivet(d, plate_x + plate_w - 8, plate_y + 7, t->edge, t->accent_hi);
        path_bg = t->panel_hi;
    }
    /* mid = h/2, the same reference every topbar element shares (see
     * topbar_power()'s own comment). This also centers the path text
     * inside the Vault Deco plate above (plate spans
     * plate_y..plate_y+plate_h, itself centered on mid when
     * plate_h=h-20). */
    int mid = h / 2;
    int path_text_h = 7 * t->metrics.body_scale;
    orion_ui_text_marquee(d, (orion_ui_rect_t){t->metrics.margin, mid - path_text_h / 2, path_max_w, 0},
                          path_s, t->text, path_bg, t->metrics.body_scale, s_now_ms);
    topbar_power(d, w);
    topbar_mem(d, w);
    if (right_status && *right_status) {
        /* Floor is the path text's own real (possibly truncated) drawn
         * width, not a fixed constant tuned for one scale step -- a fixed
         * floor can't grow with body_scale at LARGE/X-LARGE (see
         * scale_text() in ui_theme.c) and lets a longer path string
         * collide with right_status. Measured via
         * orion_ui_text_width() (the full, untruncated width) rather than
         * asking the marquee its current scroll position -- that's a
         * deliberately worst-case-safe floor: it reserves room as if the
         * path text were always shown in full, so the boundary can't drift
         * as the path's own carousel animates. */
        int path_w = imin(orion_ui_text_width(path_s, t->metrics.body_scale), path_max_w);
        int left_floor = t->metrics.margin + path_w + 24;
        /* Both functions share topbar_power_group_width() (a real,
         * computed width) rather than each guessing at the other's layout,
         * so they can't drift out of sync. */
        /* mem_reserve: topbar_mem_group_width() already returns 0 when the
         * badge is disabled, collapsing this floor back to exactly the
         * power-only case with no separate branch needed. */
        int mem_w = topbar_mem_group_width();
        int mem_reserve = mem_w > 0 ? (mem_w + 10) : 0;
        int right_limit = w - t->metrics.margin - topbar_power_group_width() - mem_reserve - 12;
        int tw = orion_ui_text_width(right_status, t->metrics.small_scale);
        int sx = right_limit - tw;
        if (sx < left_floor) sx = left_floor;
        int status_text_h = 7 * t->metrics.small_scale;
        orion_ui_text_marquee(d, (orion_ui_rect_t){sx, mid - status_text_h / 2, right_limit - sx, 0}, right_status, t->good, t->panel, t->metrics.small_scale, s_now_ms);
    }
}

/* Exposes JUST the power-badge sub-draw (already a fully self-contained
 * function with its own bg fills, see topbar_power()'s own header comment)
 * so a periodic power-state refresh touches only the ~110x28px badge,
 * nothing else on screen -- calling one of the screens' full draw
 * functions instead would start with a full orion_ui_clear() and flash
 * the entire screen for a change that only ever affects a small icon in
 * the corner. Callers must NOT wrap this in orion_ui_clear() or redraw
 * anything else alongside it -- that would defeat the entire point. */
void orion_ui_topbar_power_only(sgfx_device_t* d, int w)
{
    orion_ui_effects_prepare_draw(d);
    topbar_power(d, w);
}

void orion_ui_frame(sgfx_device_t* d, orion_ui_rect_t r,
                    orion_ui_frame_style_t style, int selected, int disabled,
                    const char* title)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    sgfx_rgba8_t fillc = disabled ? darken(t->panel, 12) : (selected ? t->panel_hi : t->panel);
    sgfx_rgba8_t edge = disabled ? t->dim : (selected ? t->edge_hi : t->edge);

    if (vault_deco_enabled() && t->style.shadow_px > 0) {
        int sh = t->style.shadow_px;
        fill(d, r.x + sh, r.y + sh, r.w, r.h, darken(t->bg, 6));
    }
    fill(d, r.x, r.y, r.w, r.h, fillc);
    rect_thick(d, r.x, r.y, r.w, r.h, imax(1, t->metrics.border_w), edge);

    if (vault_deco_enabled()) {
        if (t->style.inner_border && r.w > 14 && r.h > 14)
            rect(d, r.x + 5, r.y + 5, r.w - 10, r.h - 10, darken(t->edge_hi, 12));
        draw_corner_marks(d, r, selected ? t->accent_hi : t->edge_hi);
        if (t->style.ornament_density >= 1) {
            draw_rivet(d, r.x + 9, r.y + 9, edge, t->accent_hi);
            draw_rivet(d, r.x + r.w - 10, r.y + 9, edge, t->accent_hi);
            draw_rivet(d, r.x + 9, r.y + r.h - 10, edge, t->accent_hi);
            draw_rivet(d, r.x + r.w - 10, r.y + r.h - 10, edge, t->accent_hi);
        }
        if (t->style.ornament_density >= 3 && r.w > 120) {
            int yy = r.y + r.h - 8;
            for (int x = r.x + 30; x < r.x + r.w - 30; x += 30)
                diamond(d, x, yy, 3, 3, t->edge_hi, 0);
        }
    }

    if (selected && t->metrics.selection_style == ORION_UI_SELECT_RAIL) {
        fill(d, r.x + 2, r.y + 2, imax(3, t->metrics.accent_rail_w), r.h - 4, t->accent);
    }
    switch (style) {
    case ORION_FRAME_TITLE: {
        int ph = vault_deco_enabled() ? t->style.title_plate_h : 18;
        int pw = vault_deco_enabled() ? imin(imax(140, r.w / 3), r.w - 24) : imin(110, r.w - 16);
        int px0 = r.x + (vault_deco_enabled() ? 14 : 8);
        int py0 = r.y + (vault_deco_enabled() ? 8 : -2);
        fill(d, px0, py0, pw, ph, t->panel_hi);
        rect_thick(d, px0, py0, pw, ph, vault_deco_enabled() ? 2 : 1, edge);
        if (vault_deco_enabled() && t->style.ornament_density >= 2) {
            draw_rivet(d, px0 + 7, py0 + ph/2, edge, t->accent_hi);
            draw_rivet(d, px0 + pw - 8, py0 + ph/2, edge, t->accent_hi);
        }
        if (title) orion_ui_text_marquee(d, (orion_ui_rect_t){px0 + 14, py0 + (ph-7*t->metrics.small_scale)/2, pw - 28, 0}, title, t->accent_hi, t->panel_hi, t->metrics.small_scale, s_now_ms);
        break;
    }
    case ORION_FRAME_NOTCHED:
        fill(d, r.x + r.w - 26, r.y, 26, 10, fillc);
        line(d, r.x + r.w - 26, r.y, r.x + r.w - 8, r.y + 10, edge);
        line(d, r.x + r.w - 8, r.y + 10, r.x + r.w - 1, r.y + 10, edge);
        line(d, r.x + r.w - 26, r.y + 1, r.x + r.w - 26, r.y + 10, edge);
        break;
    case ORION_FRAME_BRACKET:
        hline(d, r.x + 8, r.y + 8, 22, edge);
        vline(d, r.x + 8, r.y + 8, 22, edge);
        hline(d, r.x + r.w - 30, r.y + 8, 22, edge);
        vline(d, r.x + r.w - 8, r.y + 8, 22, edge);
        hline(d, r.x + 8, r.y + r.h - 8, 22, edge);
        vline(d, r.x + 8, r.y + r.h - 30, 22, edge);
        hline(d, r.x + r.w - 30, r.y + r.h - 8, 22, edge);
        vline(d, r.x + r.w - 8, r.y + r.h - 30, 22, edge);
        break;
    case ORION_FRAME_STANDARD:
    default:
        break;
    }
}

void orion_ui_panel(sgfx_device_t* d, orion_ui_rect_t r, int selected)
{
    orion_ui_frame(d, r, ORION_FRAME_STANDARD, selected, 0, NULL);
}

void orion_ui_button(sgfx_device_t* d, orion_ui_rect_t r, const char* label,
                     const char* secondary, int selected, int destructive)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    orion_ui_frame(d, r, ORION_FRAME_STANDARD, selected, 0, NULL);
    if (destructive) fill(d, r.x + 1, r.y + 1, 6, r.h - 2, t->danger);
    sgfx_rgba8_t primary = destructive ? t->danger : (selected ? t->accent_hi : t->text);
    if (selected && t->metrics.selection_style == ORION_UI_SELECT_INVERT) primary = t->bg;
    int left = r.x + 18 + ((selected && t->metrics.selection_style == ORION_UI_SELECT_RAIL) ? t->metrics.accent_rail_w : 0);
    if (destructive) left += 8;
    /* Secondary-line offset scales with UI size too: the label text grows
     * with the scale selector, so the gap it sits above must grow with it
     * or the two lines collide at larger UI-size steps. */
    int sec_gap = orion_ui_scale_px(58);
    int label_y = r.y + (secondary && *secondary ? orion_ui_scale_px(22) : (r.h - 14) / 2);
    sgfx_rgba8_t btn_bg = selected ? t->panel_hi : t->panel;
    orion_ui_text_marquee(d, (orion_ui_rect_t){left, label_y, r.w - (left-r.x) - 14, 0}, label ? label : "", primary, btn_bg, t->metrics.body_scale, s_now_ms);
    if (secondary && *secondary)
        orion_ui_text_marquee(d, (orion_ui_rect_t){left, r.y + sec_gap, r.w - (left-r.x) - 14, 0}, secondary, t->dim, btn_bg, t->metrics.small_scale, s_now_ms);
}

void orion_ui_badge(sgfx_device_t* d, orion_ui_rect_t r, const char* text,
                    sgfx_rgba8_t color, int emphatic)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    sgfx_rgba8_t badge_bg = emphatic ? t->panel_hi : t->panel;
    fill(d, r.x, r.y, r.w, r.h, badge_bg);
    rect_thick(d, r.x, r.y, r.w, r.h, emphatic ? 2 : 1, color);
    if (vault_deco_enabled() && r.w > 34) {
        if (t->style.inner_border && r.w > 46 && r.h > 14) rect(d, r.x + 3, r.y + 3, r.w - 6, r.h - 6, darken(color, 24));
        if (t->style.ornament_density >= 2) {
            draw_rivet(d, r.x + 7, r.y + r.h/2, color, t->accent_hi);
            draw_rivet(d, r.x + r.w - 8, r.y + r.h/2, color, t->accent_hi);
        }
    }
    hline(d, r.x + 4, r.y + r.h - 5, 10, color);
    hline(d, r.x + r.w - 14, r.y + 4, 10, color);
    orion_ui_text_marquee(d, (orion_ui_rect_t){r.x + 8, r.y + (r.h - 7)/2, r.w - 16, 0}, text, color, badge_bg, t->metrics.small_scale, s_now_ms);
}

void orion_ui_status_chip(sgfx_device_t* d, orion_ui_rect_t r,
                          const char* text, sgfx_rgba8_t color)
{
    orion_ui_badge(d, r, text, color, 0);
}

void orion_ui_tab(sgfx_device_t* d, orion_ui_rect_t r, const char* text,
                  int selected, int disabled)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    sgfx_rgba8_t edge = disabled ? t->dim : (selected ? t->edge_hi : t->edge);
    sgfx_rgba8_t fillc = disabled ? darken(t->panel, 12) : (selected ? t->panel_hi : t->panel);
    if (vault_deco_enabled()) {
        fill(d, r.x, r.y + 3, r.w, r.h - 3, fillc);
        rect_thick(d, r.x, r.y + 3, r.w, r.h - 3, selected ? 2 : 1, edge);
        if (t->style.inner_border && r.w > 28 && r.h > 16)
            rect(d, r.x + 4, r.y + 7, r.w - 8, r.h - 11, darken(edge, 22));
        if (t->style.ornament_density >= 1) {
            draw_rivet(d, r.x + 7, r.y + r.h/2 + 1, edge, t->accent_hi);
            draw_rivet(d, r.x + r.w - 8, r.y + r.h/2 + 1, edge, t->accent_hi);
        }
        if (selected) {
            fill(d, r.x + 6, r.y + r.h - 6, r.w - 12, 3, t->accent);
            diamond(d, r.x + r.w/2, r.y + 4, 4, 4, t->accent_hi, 1);
        }
    } else {
        fill(d, r.x, r.y + 6, r.w, r.h - 6, fillc);
        rect(d, r.x, r.y + 6, r.w, r.h - 6, edge);
        hline(d, r.x + 10, r.y, r.w - 20, edge);
        line(d, r.x + 4, r.y + 6, r.x + 10, r.y, edge);
        line(d, r.x + r.w - 5, r.y + 6, r.x + r.w - 11, r.y, edge);
        if (selected) fill(d, r.x + 4, r.y + r.h - 5, r.w - 8, 3, t->accent);
    }
    orion_ui_text_marquee(d, (orion_ui_rect_t){r.x + 10, r.y + (r.h - 7*t->metrics.small_scale)/2 + 1, r.w - 20, 0},
                          text ? text : "", disabled ? t->dim : (selected ? t->accent_hi : t->text), fillc, t->metrics.small_scale, s_now_ms);
}

void orion_ui_toggle(sgfx_device_t* d, orion_ui_rect_t r, const char* text,
                     int on, int disabled, sgfx_rgba8_t bg)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    sgfx_rgba8_t edge = disabled ? t->dim : t->edge;
    sgfx_rgba8_t lit = disabled ? t->dim : (on ? t->good : t->warning);
    int tw = orion_ui_scale_px(84), th = orion_ui_scale_px(28);
    int knob = orion_ui_scale_px(28);
    orion_ui_text_marquee(d, (orion_ui_rect_t){r.x, r.y + 6, r.w - tw - 6, 0}, text ? text : "", disabled ? t->dim : t->text, bg, t->metrics.small_scale, s_now_ms);
    orion_ui_rect_t track = { r.x + r.w - tw, r.y, tw, th };
    fill(d, track.x, track.y, track.w, track.h, t->panel);
    rect_thick(d, track.x, track.y, track.w, track.h, 2, edge);
    if (vault_deco_enabled() && t->style.ornament_density >= 2) {
        draw_rivet(d, track.x + 7, track.y + track.h/2, edge, t->accent_hi);
        draw_rivet(d, track.x + track.w - 8, track.y + track.h/2, edge, t->accent_hi);
    }
    fill(d, track.x + 4, track.y + 4, track.w - 8, track.h - 8, darken(t->panel_hi, 10));
    if (on) fill(d, track.x + 5, track.y + 5, track.w/2 + 5, track.h - 10, lit);
    fill(d, on ? track.x + track.w - knob - 6 : track.x + 6, track.y + 5, knob, track.h - 10, disabled ? darken(t->text, 60) : t->accent_hi);
    rect(d, on ? track.x + track.w - knob - 6 : track.x + 6, track.y + 5, knob, track.h - 10, edge);
}

int orion_ui_toggle_hit(orion_ui_rect_t r, int x, int y)
{
    /* Whole row is the target, not just the drawn switch track -- matches
     * how these are laid out (full-width rows) and gives a real touch
     * target instead of a thin strip on the right edge. */
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void orion_ui_meter(sgfx_device_t* d, orion_ui_rect_t r, const char* label,
                    int value_pct, sgfx_rgba8_t accent, int segmented, sgfx_rgba8_t bg)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    if (value_pct < 0) value_pct = 0;
    if (value_pct > 100) value_pct = 100;
    if (label) orion_ui_text_marquee(d, (orion_ui_rect_t){r.x, r.y, r.w, 0}, label, t->dim, bg, t->metrics.small_scale, s_now_ms);
    int by = r.y + 16;
    fill(d, r.x, by, r.w, r.h - 16, t->panel);
    rect(d, r.x, by, r.w, r.h - 16, t->edge);
    if (vault_deco_enabled() && t->style.inner_border && r.w > 20 && r.h > 22)
        rect(d, r.x + 3, by + 3, r.w - 6, r.h - 22, darken(t->edge_hi, 24));
    if (segmented) {
        int segs = 10;
        int gap = 3;
        int sw = (r.w - (segs + 1) * gap) / segs;
        int lit = (value_pct + 9) / 10;
        for (int i = 0; i < segs; ++i) {
            fill(d, r.x + gap + i*(sw + gap), by + 5, sw, r.h - 26,
                 i < lit ? accent : darken(t->panel_hi, 12));
        }
    } else {
        fill(d, r.x + 4, by + 5, (r.w - 8) * value_pct / 100, r.h - 26, accent);
    }
    char buf[20];
    snprintf(buf, sizeof buf, "%d%%", value_pct);
    orion_ui_text_marquee(d, (orion_ui_rect_t){r.x + r.w - 36, r.y, 36, 0}, buf, accent, bg, t->metrics.small_scale, s_now_ms);
}

/* True delta redraw for orion_ui_meter()'s SEGMENTED style only, used by a
 * live drag (Settings' in-place backlight/keyboard-brightness sliders).
 * orion_ui_meter() itself unconditionally refills its own full panel
 * background + border on EVERY call -- fine for its other draw-once
 * callers (storage/RAM/battery meters) but a visible flash across the
 * whole bar on every touch-move sample when used for a live drag, the
 * same root-cause class draw_backlight_value() (touchmenu.c) already
 * solves for the standalone BACKLIGHT/SYSTEM VOLUME screens. Only touches
 * the segments whose lit/unlit state actually changed between prev_pct
 * and pct, plus the small percentage readout -- never the shared
 * background, border, or label text (none of which change during a drag).
 * Caller must draw the full orion_ui_meter() once first (prev_pct==-1
 * case); this only handles the update path.
 *
 * out_seg_rect/out_text_rect: a caller reapplying the CRT scanline/
 * vignette texture (a read-modify-write dim pass) MUST scope that reapply
 * to exactly the pixels this function actually touched, never the whole
 * meter rect -- since this function only repaints the CHANGED segments,
 * reapplying to the whole rect would re-dim the untouched segments at the
 * far ends on every tick with no repaint to ever undo it, darkening them
 * cumulatively forever. Returning the two DISJOINT rects that were
 * actually just touched (the segment range and the percentage text) lets
 * the caller scope its reapply correctly. Either rect has w==0 when
 * nothing in that region changed (lo==hi for the segment range) -- caller
 * must skip reapplying to a zero-width rect. */
void orion_ui_meter_delta(sgfx_device_t* d, orion_ui_rect_t r, int value_pct, int prev_pct,
                          sgfx_rgba8_t accent, sgfx_rgba8_t bg,
                          orion_ui_rect_t* out_seg_rect, orion_ui_rect_t* out_text_rect)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    if (value_pct < 0) value_pct = 0;
    if (value_pct > 100) value_pct = 100;
    int by = r.y + 16;
    int segs = 10, gap = 3;
    int sw = (r.w - (segs + 1) * gap) / segs;
    int lit = (value_pct + 9) / 10;
    int prev_lit = prev_pct >= 0 ? (prev_pct + 9) / 10 : lit;
    int lo = lit < prev_lit ? lit : prev_lit;
    int hi = lit > prev_lit ? lit : prev_lit;
    if (lo < 0) lo = 0;
    if (hi > segs) hi = segs;
    if (out_seg_rect) *out_seg_rect = (orion_ui_rect_t){0, 0, 0, 0};
    for (int i = lo; i < hi; ++i) {
        fill(d, r.x + gap + i*(sw + gap), by + 5, sw, r.h - 26,
             i < lit ? accent : darken(t->panel_hi, 12));
    }
    if (out_seg_rect && hi > lo) {
        int x0 = r.x + gap + lo*(sw + gap);
        int x1 = r.x + gap + (hi-1)*(sw + gap) + sw;
        *out_seg_rect = (orion_ui_rect_t){x0, by + 5, x1 - x0, r.h - 26};
    }
    if (out_text_rect) *out_text_rect = (orion_ui_rect_t){r.x + r.w - 36, r.y, 36, 16};
    char buf[20];
    snprintf(buf, sizeof buf, "%d%%", value_pct);
    orion_ui_text_marquee(d, (orion_ui_rect_t){r.x + r.w - 36, r.y, 36, 0}, buf, accent, bg, t->metrics.small_scale, s_now_ms);
}

void orion_ui_selector(sgfx_device_t* d, orion_ui_rect_t r,
                       const char* left, const char* center, const char* right,
                       int selected_idx, int disabled)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    const char* labels[3] = { left ? left : "A", center ? center : "B", right ? right : "C" };
    int seg_w = r.w / 3;
    for (int i = 0; i < 3; ++i) {
        orion_ui_rect_t s = { r.x + i*seg_w, r.y, (i == 2) ? r.w - 2*seg_w : seg_w, r.h };
        sgfx_rgba8_t seg_bg = disabled ? darken(t->panel, 12) : ((selected_idx == i) ? t->panel_hi : t->panel);
        fill(d, s.x, s.y, s.w, s.h, seg_bg);
        rect(d, s.x, s.y, s.w, s.h, disabled ? t->dim : t->edge);
        if (vault_deco_enabled() && t->style.ornament_density >= 2 && s.w > 28) {
            draw_rivet(d, s.x + 6, s.y + s.h/2, disabled ? t->dim : t->edge, t->accent_hi);
            draw_rivet(d, s.x + s.w - 7, s.y + s.h/2, disabled ? t->dim : t->edge, t->accent_hi);
        }
        if (selected_idx == i && !disabled) fill(d, s.x + 3, s.y + s.h - 5, s.w - 6, 3, t->accent);
        orion_ui_text_marquee(d, (orion_ui_rect_t){s.x + 8, s.y + (s.h - 7)/2, s.w - 16, 0}, labels[i],
                              disabled ? t->dim : (selected_idx == i ? t->accent_hi : t->text), seg_bg, t->metrics.small_scale, s_now_ms);
    }
}

int orion_ui_selector_hit(orion_ui_rect_t r, int x, int y)
{
    if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return -1;
    int seg_w = r.w / 3;
    int idx = (x - r.x) / (seg_w > 0 ? seg_w : 1);
    if (idx > 2) idx = 2;
    return idx;
}

void orion_ui_separator(sgfx_device_t* d, int x, int y, int w,
                        const char* label, int style)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    int label_w = label ? orion_ui_text_width(label, t->metrics.small_scale) + 16 : 0;
    int left_w = label_w ? 28 : 0;
    if (style == 0) {
        hline(d, x, y, left_w, t->edge);
        hline(d, x + left_w + label_w, y, w - left_w - label_w, t->edge);
    } else if (style == 1) {
        for (int i = 0; i < w; i += 12) hline(d, x + i, y, 6, t->edge_hi);
    } else {
        for (int i = 0; i < w; i += 18) {
            line(d, x + i, y - 3, x + i + 6, y, t->accent);
            line(d, x + i + 6, y, x + i + 12, y - 3, t->accent);
        }
    }
    if (label) {
        fill(d, x + left_w, y - 8, label_w, 16, t->bg);
        orion_ui_text_marquee(d, (orion_ui_rect_t){x + left_w + 8, y - 3, label_w - 16, 0}, label, t->dim, t->bg, t->metrics.small_scale, s_now_ms);
    }
}

void orion_ui_ornament(sgfx_device_t* d, orion_ui_ornament_t kind,
                       orion_ui_rect_t r, sgfx_rgba8_t color)
{
    int cx = r.x + r.w/2, cy = r.y + r.h/2;
    switch (kind) {
    case ORION_ORNAMENT_ATOM:
        diamond(d, cx, cy, r.w/3, r.h/4, color, 0);
        diamond(d, cx, cy, r.w/4, r.h/3, color, 0);
        fill(d, cx - 4, cy - 4, 8, 8, color);
        break;
    case ORION_ORNAMENT_RADAR:
        rect(d, r.x + 6, r.y + 6, r.w - 12, r.h - 12, color);
        line(d, r.x + 10, cy, r.x + r.w - 10, cy, color);
        line(d, cx, r.y + 10, cx, r.y + r.h - 10, color);
        line_thick(d, cx, cy, r.x + r.w - 14, r.y + 14, 2, color);
        diamond(d, cx, cy, 8, 8, color, 1);
        break;
    case ORION_ORNAMENT_CHEVRON:
        for (int i = 0; i < 3; ++i)
            line_thick(d, r.x + 10 + i*10, r.y + r.h/2 - 10,
                       r.x + 24 + i*10, r.y + r.h/2,
                       2, color),
            line_thick(d, r.x + 10 + i*10, r.y + r.h/2 + 10,
                       r.x + 24 + i*10, r.y + r.h/2,
                       2, color);
        break;
    case ORION_ORNAMENT_STARBURST:
        line_thick(d, cx - 24, cy, cx + 24, cy, 2, color);
        line_thick(d, cx, cy - 24, cx, cy + 24, 2, color);
        line_thick(d, cx - 18, cy - 18, cx + 18, cy + 18, 2, color);
        line_thick(d, cx - 18, cy + 18, cx + 18, cy - 18, 2, color);
        diamond(d, cx, cy, 6, 6, color, 1);
        break;
    }
}


/* ----- pictograms: separate 48px and 32px raster masters ----- */
static void bp_set(uint8_t* pix, int s, int x, int y)
{
    if ((unsigned)x < (unsigned)s && (unsigned)y < (unsigned)s) pix[y * s + x] = 1;
}
static void bp_hline(uint8_t* pix, int s, int x, int y, int w)
{
    for (int i = 0; i < w; ++i) bp_set(pix, s, x + i, y);
}
static void bp_vline(uint8_t* pix, int s, int x, int y, int h)
{
    for (int i = 0; i < h; ++i) bp_set(pix, s, x, y + i);
}
static void bp_fill(uint8_t* pix, int s, int x, int y, int w, int h)
{
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) bp_set(pix, s, x + xx, y + yy);
}
static void bp_rect(uint8_t* pix, int s, int x, int y, int w, int h, int th)
{
    for (int i = 0; i < th; ++i) {
        bp_hline(pix, s, x + i, y + i, w - 2 * i);
        bp_hline(pix, s, x + i, y + h - 1 - i, w - 2 * i);
        bp_vline(pix, s, x + i, y + i, h - 2 * i);
        bp_vline(pix, s, x + w - 1 - i, y + i, h - 2 * i);
    }
}
static void bp_line(uint8_t* pix, int s, int x0, int y0, int x1, int y1, int th)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int ox = -th / 2; ox <= th / 2; ++ox)
            for (int oy = -th / 2; oy <= th / 2; ++oy)
                bp_set(pix, s, x0 + ox, y0 + oy);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void blit_master(sgfx_device_t* d, int x, int y, int slot, int master,
                        const uint8_t* pix, sgfx_rgba8_t color)
{
    int offx = x + (slot - master) / 2;
    int offy = y + (slot - master) / 2;
    for (int yy = 0; yy < master; ++yy)
        for (int xx = 0; xx < master; ++xx)
            if (pix[yy * master + xx]) sgfx_fill_rect(d, offx + xx, offy + yy, 1, 1, color);
}

static void draw_terminal48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,6,6,36,24,3); bp_hline(pix,48,11,12,26); bp_line(pix,48,15,12,22,18,3); bp_line(pix,48,15,24,22,18,3); bp_hline(pix,48,27,24,9); bp_fill(pix,48,21,31,6,4); bp_fill(pix,48,15,36,18,4);} 
static void draw_terminal32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,4,4,24,16,2); bp_hline(pix,32,8,8,16); bp_line(pix,32,11,8,16,12,1); bp_line(pix,32,11,16,16,12,1); bp_hline(pix,32,18,16,5); bp_fill(pix,32,14,21,4,2); bp_fill(pix,32,10,25,12,3);} 

static void draw_video48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,7,13,28,18,2); bp_fill(pix,48,12,17,8,10); bp_line(pix,48,20,17,29,22,2); bp_line(pix,48,20,27,29,22,2); bp_rect(pix,48,34,17,7,10,2); bp_fill(pix,48,16,34,12,3);} 
static void draw_video32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,5,9,19,12,2); bp_fill(pix,32,9,12,6,7); bp_line(pix,32,15,12,21,16,1); bp_line(pix,32,15,19,21,16,1); bp_rect(pix,32,24,12,4,6,1); bp_fill(pix,32,10,23,9,2);} 

static void draw_settings48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,12,12,24,24,2); bp_rect(pix,48,17,17,14,14,2); bp_fill(pix,48,22,10,4,6); bp_fill(pix,48,22,32,4,6); bp_fill(pix,48,10,22,6,4); bp_fill(pix,48,32,22,6,4); bp_fill(pix,48,23,20,2,8);} 
static void draw_settings32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,8,8,16,16,2); bp_rect(pix,32,11,11,10,10,1); bp_fill(pix,32,14,6,4,4); bp_fill(pix,32,14,22,4,4); bp_fill(pix,32,6,14,4,4); bp_fill(pix,32,22,14,4,4); bp_fill(pix,32,15,13,2,6);} 

static void draw_display48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,7,10,34,22,3); bp_hline(pix,48,13,16,22); bp_hline(pix,48,13,21,22); bp_hline(pix,48,13,26,22); bp_fill(pix,48,22,33,4,4); bp_fill(pix,48,16,38,16,3);} 
static void draw_display32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,5,7,22,15,2); bp_hline(pix,32,9,11,14); bp_hline(pix,32,9,15,14); bp_hline(pix,32,9,19,14); bp_fill(pix,32,14,23,4,3); bp_fill(pix,32,10,27,12,2);} 

static void draw_hardware48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,14,14,20,20,2); bp_fill(pix,48,20,20,8,8); bp_vline(pix,48,18,8,6); bp_vline(pix,48,24,8,6); bp_vline(pix,48,30,8,6); bp_vline(pix,48,18,34,6); bp_vline(pix,48,24,34,6); bp_vline(pix,48,30,34,6); bp_hline(pix,48,8,18,6); bp_hline(pix,48,8,24,6); bp_hline(pix,48,8,30,6); bp_hline(pix,48,34,18,6); bp_hline(pix,48,34,24,6); bp_hline(pix,48,34,30,6);} 
static void draw_hardware32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,9,9,14,14,2); bp_fill(pix,32,13,13,6,6); bp_vline(pix,32,12,4,5); bp_vline(pix,32,16,4,5); bp_vline(pix,32,20,4,5); bp_vline(pix,32,12,23,5); bp_vline(pix,32,16,23,5); bp_vline(pix,32,20,23,5); bp_hline(pix,32,4,12,5); bp_hline(pix,32,4,16,5); bp_hline(pix,32,4,20,5); bp_hline(pix,32,23,12,5); bp_hline(pix,32,23,16,5); bp_hline(pix,32,23,20,5);} 

static void draw_network48(uint8_t* pix){ memset(pix,0,48*48); bp_fill(pix,48,22,10,4,5); bp_fill(pix,48,23,15,2,15); bp_line(pix,48,24,20,16,34,2); bp_line(pix,48,24,20,32,34,2); bp_hline(pix,48,14,34,20); bp_line(pix,48,18,14,14,18,2); bp_line(pix,48,14,18,18,22,2); bp_line(pix,48,30,14,34,18,2); bp_line(pix,48,34,18,30,22,2); bp_line(pix,48,12,11,8,18,1); bp_line(pix,48,8,18,12,25,1); bp_line(pix,48,36,11,40,18,1); bp_line(pix,48,40,18,36,25,1);} 
static void draw_network32(uint8_t* pix){ memset(pix,0,32*32); bp_fill(pix,32,15,4,2,4); bp_fill(pix,32,15,8,2,9); bp_line(pix,32,16,12,11,23,1); bp_line(pix,32,16,12,21,23,1); bp_hline(pix,32,10,23,13); bp_line(pix,32,12,8,10,11,1); bp_line(pix,32,10,11,12,14,1); bp_line(pix,32,20,8,22,11,1); bp_line(pix,32,22,11,20,14,1); bp_line(pix,32,8,6,6,10,1); bp_line(pix,32,24,6,26,10,1);} 

static void draw_diag48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,8,12,32,22,2); bp_hline(pix,48,14,17,20); bp_line(pix,48,14,26,19,26,2); bp_line(pix,48,19,26,24,19,2); bp_line(pix,48,24,19,29,28,2); bp_line(pix,48,29,28,34,22,2);} 
static void draw_diag32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,6,9,20,14,2); bp_hline(pix,32,10,12,12); bp_line(pix,32,10,19,13,19,1); bp_line(pix,32,13,19,16,14,1); bp_line(pix,32,16,14,19,20,1); bp_line(pix,32,19,20,22,16,1);} 

static void draw_theme48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,7,11,26,10,2); bp_fill(pix,48,11,14,18,4); bp_line(pix,48,33,16,38,16,2); bp_line(pix,48,38,16,38,26,2); bp_line(pix,48,38,26,24,33,2); bp_fill(pix,48,18,32,9,10);} 
static void draw_theme32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,5,8,18,7,1); bp_fill(pix,32,8,10,12,3); bp_line(pix,32,23,11,26,11,1); bp_line(pix,32,26,11,26,18,1); bp_line(pix,32,26,18,17,23,1); bp_fill(pix,32,13,22,6,7);} 

static void draw_storage48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,12,12,24,24,2); bp_fill(pix,48,16,17,16,5); bp_hline(pix,48,16,26,16); bp_hline(pix,48,16,31,16); bp_fill(pix,48,21,37,6,3);} 
static void draw_storage32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,8,9,16,16,2); bp_fill(pix,32,10,12,12,3); bp_hline(pix,32,10,19,12); bp_hline(pix,32,10,22,12); bp_fill(pix,32,13,25,6,2);} 

static void draw_keyboard48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,6,14,36,20,2); for(int r=0;r<2;r++) for(int c=0;c<5;c++) bp_fill(pix,48,11+c*6,18+r*6,4,4); bp_fill(pix,48,14,30,20,3);} 
static void draw_keyboard32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,4,10,24,14,2); for(int r=0;r<2;r++) for(int c=0;c<4;c++) bp_fill(pix,32,7+c*5,13+r*4,3,2); bp_fill(pix,32,10,21,12,2);} 

static void draw_info48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,12,9,24,30,2); bp_fill(pix,48,22,14,4,4); bp_fill(pix,48,21,21,6,11); bp_hline(pix,48,19,34,10);} 
static void draw_info32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,8,6,16,20,2); bp_fill(pix,32,14,10,3,3); bp_fill(pix,32,13,15,5,7); bp_hline(pix,32,12,23,8);} 

static void draw_audio48(uint8_t* pix){ memset(pix,0,48*48); bp_fill(pix,48,9,17,7,14); bp_line(pix,48,16,17,28,10,2); bp_line(pix,48,16,31,28,38,2); bp_line(pix,48,29,17,39,24,2); bp_line(pix,48,29,31,39,24,2);} 
static void draw_audio32(uint8_t* pix){ memset(pix,0,32*32); bp_fill(pix,32,8,12,5,8); bp_line(pix,32,13,12,20,9,1); bp_line(pix,32,13,20,20,23,1); bp_line(pix,32,21,12,26,16,1); bp_line(pix,32,21,20,26,16,1);} 

static void draw_camera48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,8,14,32,22,2); bp_fill(pix,48,14,10,12,6); bp_rect(pix,48,19,19,12,12,2); bp_fill(pix,48,22,22,6,6); bp_fill(pix,48,33,18,4,4);} 
static void draw_camera32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,5,10,22,15,2); bp_fill(pix,32,9,7,8,4); bp_rect(pix,32,12,13,8,8,1); bp_fill(pix,32,14,15,4,4); bp_fill(pix,32,23,13,3,3);} 

static void draw_power48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,11,10,26,28,3); bp_fill(pix,48,19,7,10,4); bp_line(pix,48,25,15,20,25,2); bp_hline(pix,48,20,25,6); bp_line(pix,48,26,25,22,33,2);} 
static void draw_power32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,7,7,18,20,2); bp_fill(pix,32,12,4,8,4); bp_line(pix,32,17,11,13,18,1); bp_hline(pix,32,13,18,5); bp_line(pix,32,18,18,15,24,1);} 

static void draw_gamepad48(uint8_t* pix){ memset(pix,0,48*48); bp_rect(pix,48,8,14,32,18,3); bp_fill(pix,48,5,20,9,14); bp_fill(pix,48,34,20,9,14); bp_hline(pix,48,15,23,10); bp_vline(pix,48,20,18,10); bp_fill(pix,48,29,21,5,5); bp_fill(pix,48,35,17,5,5);} 
static void draw_gamepad32(uint8_t* pix){ memset(pix,0,32*32); bp_rect(pix,32,5,9,22,14,2); bp_fill(pix,32,3,13,6,10); bp_fill(pix,32,23,13,6,10); bp_hline(pix,32,10,16,7); bp_vline(pix,32,13,13,7); bp_fill(pix,32,19,14,4,4); bp_fill(pix,32,24,11,4,4);} 

static void draw_master_for_icon(orion_ui_icon_t icon, int master, uint8_t* pix)
{
    if (master == 48) {
        switch (icon) {
        case ORION_ICON_TERMINAL: draw_terminal48(pix); break;
        case ORION_ICON_VIDEO: draw_video48(pix); break;
        case ORION_ICON_SETTINGS: draw_settings48(pix); break;
        case ORION_ICON_DISPLAY: draw_display48(pix); break;
        case ORION_ICON_HARDWARE: draw_hardware48(pix); break;
        case ORION_ICON_NETWORK: draw_network48(pix); break;
        case ORION_ICON_DIAGNOSTICS: draw_diag48(pix); break;
        case ORION_ICON_THEME: draw_theme48(pix); break;
        case ORION_ICON_STORAGE: draw_storage48(pix); break;
        case ORION_ICON_KEYBOARD: draw_keyboard48(pix); break;
        case ORION_ICON_INFO: draw_info48(pix); break;
        case ORION_ICON_AUDIO: draw_audio48(pix); break;
        case ORION_ICON_CAMERA: draw_camera48(pix); break;
        case ORION_ICON_POWER: draw_power48(pix); break;
        case ORION_ICON_GAMEPAD: draw_gamepad48(pix); break;
        default: memset(pix, 0, 48 * 48); break;
        }
    } else {
        switch (icon) {
        case ORION_ICON_TERMINAL: draw_terminal32(pix); break;
        case ORION_ICON_VIDEO: draw_video32(pix); break;
        case ORION_ICON_SETTINGS: draw_settings32(pix); break;
        case ORION_ICON_DISPLAY: draw_display32(pix); break;
        case ORION_ICON_HARDWARE: draw_hardware32(pix); break;
        case ORION_ICON_NETWORK: draw_network32(pix); break;
        case ORION_ICON_DIAGNOSTICS: draw_diag32(pix); break;
        case ORION_ICON_THEME: draw_theme32(pix); break;
        case ORION_ICON_STORAGE: draw_storage32(pix); break;
        case ORION_ICON_KEYBOARD: draw_keyboard32(pix); break;
        case ORION_ICON_INFO: draw_info32(pix); break;
        case ORION_ICON_AUDIO: draw_audio32(pix); break;
        case ORION_ICON_CAMERA: draw_camera32(pix); break;
        case ORION_ICON_POWER: draw_power32(pix); break;
        case ORION_ICON_GAMEPAD: draw_gamepad32(pix); break;
        default: memset(pix, 0, 32 * 32); break;
        }
    }
}

void orion_ui_debug_get_icon_master(orion_ui_icon_t icon, int master, uint8_t* out_pixels)
{
    if (!out_pixels) return;
    if (master == 48) draw_master_for_icon(icon, 48, out_pixels);
    else draw_master_for_icon(icon, 32, out_pixels);
}

void orion_ui_icon(sgfx_device_t* d, orion_ui_icon_t icon, int x, int y,
                   int size, sgfx_rgba8_t color)
{
    if (!d || size < 12) return;
    if (icon < 0 || icon >= ORION_ICON_COUNT) {
        orion_ui_missing_asset(d, (orion_ui_rect_t){x, y, size, size}, "ICON");
        return;
    }
    if (size >= 40) {
        uint8_t pix[48 * 48];
        draw_master_for_icon(icon, 48, pix);
        blit_master(d, x, y, size, 48, pix, color);
    } else {
        uint8_t pix[32 * 32];
        draw_master_for_icon(icon, 32, pix);
        blit_master(d, x, y, size, 32, pix, color);
    }
}

void orion_ui_missing_asset(sgfx_device_t* d, orion_ui_rect_t r, const char* name)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    fill(d, r.x, r.y, r.w, r.h, t->panel);
    rect_thick(d, r.x, r.y, r.w, r.h, 2, t->placeholder);
    for (int i = 0; i < imin(r.w, r.h); i += 6) {
        fill(d, r.x + i, r.y + i, 3, 3, t->placeholder);
        fill(d, r.x + r.w - 4 - i, r.y + i, 3, 3, t->placeholder);
    }
    char buf[56];
    snprintf(buf, sizeof buf, "MISSING:%s", name ? name : "ASSET");
    orion_ui_text_marquee(d, (orion_ui_rect_t){r.x + 6, r.y + r.h/2 - 3, r.w - 12, 0}, buf, t->placeholder, t->panel, t->metrics.small_scale, s_now_ms);
}

/* ── schema-3 CRT/effects compositor ──────────────────────────────────── */
typedef struct {
    uint8_t* fb;
    size_t stride;
    int phys_w, phys_h;
    int landscape; /* logical width/height are swapped relative to scanout */
} orion_fx_surface_t;

typedef struct {
    uint16_t* backup;
    size_t backup_cap_px;
    int valid;
    int x, y, w, h; /* physical scanout coordinates */
} orion_fx_runtime_t;

static orion_fx_runtime_t s_fx;

static int fx_surface(sgfx_device_t* d, orion_fx_surface_t* out)
{
    if (!d || !out || !d->drv || !d->drv->get_fb_ptr) return 0;
    size_t stride = 0;
    uint8_t* fb = (uint8_t*)d->drv->get_fb_ptr(d, &stride);
    if (!fb || stride < 2) return 0;
    int phys_w = (int)(stride / 2u);
    int phys_h;
    int landscape = 0;
    if (phys_w == (int)d->caps.width) {
        phys_h = (int)d->caps.height;
    } else if (phys_w == (int)d->caps.height) {
        /* Tab5's direct framebuffer stays physically portrait while SGFX's
         * logical caps swap at 90/270 degrees. The dynamic effect only needs
         * to know whether a logical horizontal band maps to a physical row or
         * column; the sign of the rotation merely reverses sweep direction and
         * is visually equivalent. */
        phys_h = (int)d->caps.width;
        landscape = 1;
    } else {
        return 0;
    }
    *out = (orion_fx_surface_t){fb, stride, phys_w, phys_h, landscape};
    return 1;
}

static uint16_t fx_pack565(sgfx_rgba8_t c)
{
    return (uint16_t)(((c.r & 0xF8u) << 8) | ((c.g & 0xFCu) << 3) | (c.b >> 3));
}

static uint16_t fx_scale565(uint16_t p, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 160) pct = 160;
    int r = ((p >> 11) & 31) * pct / 100;
    int g = ((p >> 5) & 63) * pct / 100;
    int b = (p & 31) * pct / 100;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t fx_mix565(uint16_t a, uint16_t b, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int ar=(a>>11)&31, ag=(a>>5)&63, ab=a&31;
    int br=(b>>11)&31, bg=(b>>5)&63, bb=b&31;
    int r=(ar*(100-pct)+br*pct)/100;
    int g=(ag*(100-pct)+bg*pct)/100;
    int bl=(ab*(100-pct)+bb*pct)/100;
    return (uint16_t)((r<<11)|(g<<5)|bl);
}

static uint32_t fx_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void fx_flush(sgfx_device_t* d, int x, int y, int w, int h)
{
    if (d && d->drv && d->drv->flush_surface && w > 0 && h > 0)
        (void)d->drv->flush_surface(d, x, y, w, h);
}

void orion_ui_effects_reset(void)
{
    s_fx.valid = 0;
}

/* do_flush=0 variant used only by orion_ui_effects_tick() itself (see its
 * own header comment for why): it restores the old band and later draws a
 * new one in the SAME call, so flushing here separately from that would
 * create a real, hardware-visible intermediate "band removed" frame. Every
 * OTHER caller goes through fx_restore_previous() below (do_flush=1,
 * unchanged) since they draw their own unrelated content afterward and
 * rely on this flush to make the restored region visible at all -- their
 * own subsequent sgfx_present() is dirty-rect-scoped to what THEY drew via
 * normal SGFX primitives and has no idea this raw-framebuffer region needs
 * flushing too. */
static void fx_restore_previous_impl(sgfx_device_t* d, const orion_fx_surface_t* sf, int do_flush)
{
    if (!s_fx.valid || !s_fx.backup) return;
    for (int yy=0; yy<s_fx.h; ++yy) {
        uint16_t* dst=(uint16_t*)(sf->fb+(size_t)(s_fx.y+yy)*sf->stride)+(size_t)s_fx.x;
        memcpy(dst, s_fx.backup + (size_t)yy*(size_t)s_fx.w, (size_t)s_fx.w*2u);
    }
    if (do_flush) fx_flush(d, s_fx.x, s_fx.y, s_fx.w, s_fx.h);
    s_fx.valid = 0;
}

static void fx_restore_previous(sgfx_device_t* d, const orion_fx_surface_t* sf)
{
    fx_restore_previous_impl(d, sf, 1);
}

/* Called before any UI code that repaints only part of an already-visible
 * screen. It removes the moving band first so the UI redraw never paints on
 * top of a transient effect and, equally importantly, the next effect tick
 * never restores stale pixels over newly-updated content. */
void orion_ui_effects_prepare_draw(sgfx_device_t* d)
{
    orion_fx_surface_t sf;
    if (fx_surface(d, &sf)) fx_restore_previous(d, &sf);
    else s_fx.valid = 0;
}

static int fx_capture_region(const orion_fx_surface_t* sf, int x, int y, int w, int h)
{
    size_t need=(size_t)w*(size_t)h;
    if (need > s_fx.backup_cap_px) {
        /* This is a high-water-mark buffer -- it only ever grows, never
         * shrinks. A CRT-effect theme's animated refresh-bar sweep
         * (orion_ui_effects_tick()) captures a `band x full physical
         * dimension` region every tick, so this buffer quickly grows to its
         * full size (tens of KB) the first time such a theme is used, and
         * then permanently claims that much RAM for the rest of the boot
         * session. Allocated from PSRAM specifically, not internal RAM --
         * internal RAM is scarce and shared with the video player's own
         * I2S/DMA descriptor allocation and H.264 decoder setup, which need
         * a large contiguous internal block later in the same session.
         * This buffer is touched only
         * ~10-20x/sec (the effect tick rate), far too cold to need internal-
         * RAM speed. Guarded to ESP_PLATFORM only -- this file is also
         * linked into the host-only tools/build-ui-preview.sh binary via
         * plain cc, which has no esp_heap_caps.h. */
#ifdef ESP_PLATFORM
        uint16_t* p=(uint16_t*)heap_caps_realloc(s_fx.backup, need*2u,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        uint16_t* p=(uint16_t*)realloc(s_fx.backup, need*2u);
#endif
        if (!p) { s_fx.valid=0; return 0; }
        s_fx.backup=p; s_fx.backup_cap_px=need;
    }
    for (int yy=0; yy<h; ++yy) {
        const uint16_t* src=(const uint16_t*)(sf->fb+(size_t)(y+yy)*sf->stride)+(size_t)x;
        memcpy(s_fx.backup+(size_t)yy*(size_t)w, src, (size_t)w*2u);
    }
    s_fx.x=x; s_fx.y=y; s_fx.w=w; s_fx.h=h; s_fx.valid=1;
    return 1;
}

/* x0,y0,x1,y1: physical scanout bounds to touch (exclusive on x1/y1),
 * defaulting to the whole surface for the original full-redraw callers.
 * Region-scoped calls (orion_ui_scanlines_phys_region(), for a partial
 * redraw like a scroll) skip the noise pass -- it is sparse,
 * whole-frame grain meant to read as a constant background presence, not
 * something that needs to track a moving clip rect precisely. Vignette's
 * own distance-from-edge math intentionally still measures against the
 * FULL sf->phys_w/phys_h (the true panel edges), only the loop bounds
 * shrink -- a re-scanned sub-region must still darken toward the real
 * screen edge, not the edge of whatever rect happened to be redrawn. */
static void fx_static_direct(sgfx_device_t* d, const orion_ui_theme_t* t,
                             const orion_fx_surface_t* sf,
                             int x0, int y0, int x1, int y1, int include_noise)
{
    (void)d;
    const orion_ui_effects_t* e=&t->effects;
    if (x0<0) x0=0;
    if (y0<0) y0=0;
    if (x1>sf->phys_w) x1=sf->phys_w;
    if (y1>sf->phys_h) y1=sf->phys_h;
    if (x0>=x1 || y0>=y1) return;
    int scan_on = (e->overlay==ORION_UI_OVERLAY_CRT && e->scanlines) || t->metrics.scanline_alpha>0;
    int scan_strength = e->scanline_strength>0 ? e->scanline_strength : t->metrics.scanline_alpha;
    int pitch = e->scanline_pitch>1 ? e->scanline_pitch : 4;

    if (scan_on && scan_strength>0) {
        int pct=100-scan_strength;
        if (!sf->landscape) {
            int y_start = y0 + ((1 - y0) % pitch + pitch) % pitch;
            for (int y=y_start; y<y1; y+=pitch) {
                uint16_t* row=(uint16_t*)(sf->fb+(size_t)y*sf->stride);
                for (int x=x0; x<x1; ++x) row[x]=fx_scale565(row[x],pct);
            }
        } else {
            /* logical horizontal scanlines become physical columns after the
             * UI rotates into the portrait scanout framebuffer. */
            int x_start = x0 + ((1 - x0) % pitch + pitch) % pitch;
            for (int x=x_start; x<x1; x+=pitch) {
                for (int y=y0; y<y1; ++y) {
                    uint16_t* p=(uint16_t*)(sf->fb+(size_t)y*sf->stride)+(size_t)x;
                    *p=fx_scale565(*p,pct);
                }
            }
        }
    }

    if (e->overlay==ORION_UI_OVERLAY_CRT && e->vignette && e->vignette_strength>0) {
        int vw=e->vignette_width;
        if (vw<1) vw=1;
        for (int y=y0; y<y1; ++y) {
            uint16_t* row=(uint16_t*)(sf->fb+(size_t)y*sf->stride);
            int dy=imin(y, sf->phys_h-1-y);
            for (int x=x0; x<x1; ++x) {
                int dx=imin(x, sf->phys_w-1-x);
                int dist=imin(dx,dy);
                if (dist>=vw) continue;
                int strength=e->vignette_strength*(vw-dist)/vw;
                row[x]=fx_scale565(row[x],100-strength);
            }
        }
    }

    /* Sparse deterministic grain on full redraw: enough to break the flat
     * digital surface without turning the UI into a VHS preset. Animated
     * grain inside the moving band is handled in orion_ui_effects_tick(). */
    if (include_noise && e->overlay==ORION_UI_OVERLAY_CRT && e->noise && e->noise_density>0 && e->noise_strength>0) {
        uint32_t seed=0x5641554cu ^ s_now_ms; /* "VAUL" */
        int rw=x1-x0, rh=y1-y0;
        int points=(rw*rh*e->noise_density)/12000;
        if (points>2500) points=2500;
        for (int i=0;i<points;++i) {
            seed=fx_hash(seed+(uint32_t)i+1u);
            int x=x0+(int)(seed%(uint32_t)rw);
            seed=fx_hash(seed+0x9e3779b9u);
            int y=y0+(int)(seed%(uint32_t)rh);
            uint16_t* p=(uint16_t*)(sf->fb+(size_t)y*sf->stride)+(size_t)x;
            int delta=(int)(seed%3u)-1;
            int pct=100+delta*e->noise_strength;
            *p=fx_scale565(*p,pct);
        }
    }
}

void orion_ui_scanlines(sgfx_device_t* d, int w, int h)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    orion_fx_surface_t sf;
    if (fx_surface(d,&sf)) {
        fx_static_direct(d,t,&sf,0,0,sf.phys_w,sf.phys_h,1);
        /* Full-screen draws already marked the whole logical frame dirty, so
         * firmware will cache-flush it on the immediately-following present.
         * Host direct-framebuffer previews need no explicit dirty bookkeeping. */
        return;
    }

    /* Portable fallback for non-direct-framebuffer targets: preserve the old
     * scanline treatment. Vignette/noise require pixel read/modify/write and
     * therefore intentionally degrade away here instead of allocating a full
     * shadow framebuffer. */
    int a = t->effects.scanline_strength>0 ? t->effects.scanline_strength : t->metrics.scanline_alpha;
    int pitch = t->effects.scanline_pitch>1 ? t->effects.scanline_pitch : 4;
    if (a <= 0 || !((t->effects.overlay==ORION_UI_OVERLAY_CRT && t->effects.scanlines) || t->metrics.scanline_alpha>0)) return;
    sgfx_rgba8_t c = {
        (uint8_t)((t->bg.r*(255-a)+t->edge.r*a)/255),
        (uint8_t)((t->bg.g*(255-a)+t->edge.g*a)/255),
        (uint8_t)((t->bg.b*(255-a)+t->edge.b*a)/255), 255
    };
    for (int y=1; y<h; y+=pitch) fill(d,0,y,w,1,c);
}

/* orion_ui_scanlines() only ever gets called on FULL redraws (see its
 * callers' `if (full)` guards throughout ui_screens.c) -- a partial
 * redraw that clears and repaints just a sub-rect (the launcher's own
 * scroll path, orion_ui_draw_launcher()'s `!full` branch) would otherwise
 * erase whatever scanline/vignette texture was baked into that region
 * without reapplying it, visibly losing CRT texture in the
 * actively-scrolling area while the untouched chrome around it keeps it.
 * Calling the full orion_ui_scanlines(d,w,h) after every scroll tick would
 * fix the visual gap but reintroduces a full-screen touch on every
 * throttled redraw, reintroducing the flicker the scroll throttle exists
 * to avoid. This re-runs the same scanline/vignette math bounded to just
 * the PHYSICAL rect that was actually redrawn.
 *
 * Takes PHYSICAL scanout coordinates, not logical ones -- ui_components.c
 * is deliberately kept host-buildable (see tools/build-ui-preview.sh, which
 * links this file with no display.c/orion.h dependency), so the
 * logical->physical rotation transform is the CALLER's job (touchmenu.c,
 * a firmware-only file that already has orion_display_logical_to_phys()
 * for exactly this). This function only does bounded pixel math -- no
 * rotation knowledge needed here, so nothing about host-buildability
 * changes. Noise is skipped for scoped calls (see fx_static_direct()'s own
 * comment on include_noise). No-ops on non-direct-framebuffer targets --
 * there's no clip-scoped equivalent of the portable fallback below, a
 * known gap for SGFX targets whose DSI panel drivers have no fill_rect
 * rather than a new one added silently. */
void orion_ui_scanlines_phys_region(sgfx_device_t* d, int px, int py, int pw, int ph)
{
    const orion_ui_theme_t* t = orion_ui_theme();
    orion_fx_surface_t sf;
    if (!fx_surface(d,&sf)) return;
    fx_static_direct(d,t,&sf,px,py,px+pw,py+ph,0);
    fx_flush(d,px,py,pw,ph);
}

/* True delta-redraw primitive. A redraw throttle alone, and a scanline-
 * reapply-every-tick approach, both proved insufficient for launcher
 * scroll flicker (the reapply approach regressed responsiveness by running
 * a per-pixel pass inside the blocking call chain touch sampling waits
 * on). Shifts an already-scanned-out PHYSICAL region
 * by phys_shift pixels along ONE axis, so previously-drawn pixels move to
 * their new on-screen position instead of being re-rendered from scratch --
 * the caller only needs to draw the strip newly exposed at the leading
 * edge, not the whole region.
 *
 * axis_is_x=0: shift PHYSICAL ROWS (cross-row copy, one memmove per row).
 * axis_is_x=1: shift each row's own pixels HORIZONTALLY (memmove WITHIN
 * each row) -- this is what a LOGICAL vertical scroll needs on Tab5's
 * actual real-world orientation (landscape, rotation 1 or 3), where
 * logical Y maps to the physical X axis. Both cases reduce to one memmove
 * per row -- row-major framebuffer memory makes a same-row horizontal
 * shift just as cheap as a cross-row vertical one; there is no per-pixel
 * loop here regardless of orientation.
 *
 * Positive phys_shift moves content toward increasing physical coordinate
 * (row/col N's content ends up at N+phys_shift); the caller derives the
 * correct sign for whichever rotation is active (see
 * orion_ui_launcher_content_rect()'s caller in touchmenu.c, which derives
 * axis/sign from orion_display_logical_to_phys() directly rather than
 * duplicating display.c's own rotation-case formulas here). Returns 0 (no
 * shift performed -- caller must fall back to a full redraw) on a
 * non-direct-framebuffer target or when |phys_shift| would leave nothing
 * of the region unshifted. */
int orion_ui_fb_shift_phys_region(sgfx_device_t* d, int px, int py, int pw, int ph,
                                  int axis_is_x, int phys_shift)
{
    orion_fx_surface_t sf;
    if (!fx_surface(d,&sf)) return 0;
    if (phys_shift == 0) return 1;
    if (px<0 || py<0 || pw<=0 || ph<=0 || px+pw>sf.phys_w || py+ph>sf.phys_h) return 0;

    if (!axis_is_x) {
        int absn = phys_shift<0 ? -phys_shift : phys_shift;
        if (absn >= ph) return 0;
        if (phys_shift > 0) {
            /* Content moves to higher rows -- process destinations
             * high-to-low so each source row is read before any earlier
             * iteration could have overwritten it (see this function's own
             * design comment history for the proof; rows are independent,
             * non-overlapping stride blocks, so only cross-iteration
             * ordering matters, not any single memmove's own aliasing). */
            for (int y=ph-1; y>=phys_shift; --y) {
                uint16_t* dst=(uint16_t*)(sf.fb+(size_t)(py+y)*sf.stride)+(size_t)px;
                const uint16_t* src=(const uint16_t*)(sf.fb+(size_t)(py+y-phys_shift)*sf.stride)+(size_t)px;
                memmove(dst,src,(size_t)pw*2u);
            }
        } else {
            int n=-phys_shift;
            for (int y=0; y<ph-n; ++y) {
                uint16_t* dst=(uint16_t*)(sf.fb+(size_t)(py+y)*sf.stride)+(size_t)px;
                const uint16_t* src=(const uint16_t*)(sf.fb+(size_t)(py+y+n)*sf.stride)+(size_t)px;
                memmove(dst,src,(size_t)pw*2u);
            }
        }
    } else {
        int absn = phys_shift<0 ? -phys_shift : phys_shift;
        if (absn >= pw) return 0;
        for (int y=0; y<ph; ++y) {
            uint16_t* row=(uint16_t*)(sf.fb+(size_t)(py+y)*sf.stride)+(size_t)px;
            if (phys_shift > 0) memmove(row+phys_shift, row, (size_t)(pw-phys_shift)*2u);
            else                memmove(row, row-phys_shift, (size_t)(pw+phys_shift)*2u);
        }
    }
    fx_flush(d,px,py,pw,ph);
    return 1;
}

static void fx_shift_band(const orion_fx_surface_t* sf, int amount)
{
    if (amount==0 || !s_fx.valid) return;
    if (!sf->landscape) {
        int absn=amount<0?-amount:amount;
        if (absn>=s_fx.w) return;
        for (int yy=0; yy<s_fx.h; ++yy) {
            uint16_t* row=(uint16_t*)(sf->fb+(size_t)(s_fx.y+yy)*sf->stride)+(size_t)s_fx.x;
            const uint16_t* src=s_fx.backup+(size_t)yy*(size_t)s_fx.w;
            if (amount>0) {
                memcpy(row+amount,src,(size_t)(s_fx.w-amount)*2u);
                memcpy(row,src,(size_t)amount*2u);
            } else {
                int n=-amount;
                memcpy(row,src+n,(size_t)(s_fx.w-n)*2u);
                memcpy(row+s_fx.w-n,src+s_fx.w-n,(size_t)n*2u);
            }
        }
    } else {
        /* Logical X maps to physical Y in landscape. Shift each physical
         * column vertically, sourcing from the pristine captured strip. */
        int n=amount<0?-amount:amount;
        if (n>=s_fx.h) return;
        for (int xx=0; xx<s_fx.w; ++xx) {
            if (amount>0) {
                for (int yy=s_fx.h-1; yy>=n; --yy) {
                    uint16_t* dst=(uint16_t*)(sf->fb+(size_t)(s_fx.y+yy)*sf->stride)+(size_t)(s_fx.x+xx);
                    *dst=s_fx.backup[(size_t)(yy-n)*(size_t)s_fx.w+(size_t)xx];
                }
            } else {
                for (int yy=0; yy<s_fx.h-n; ++yy) {
                    uint16_t* dst=(uint16_t*)(sf->fb+(size_t)(s_fx.y+yy)*sf->stride)+(size_t)(s_fx.x+xx);
                    *dst=s_fx.backup[(size_t)(yy+n)*(size_t)s_fx.w+(size_t)xx];
                }
            }
        }
    }
}

int orion_ui_effects_tick(sgfx_device_t* d, uint32_t now_ms)
{
    const orion_ui_theme_t* t=orion_ui_theme();
    const orion_ui_effects_t* e=&t->effects;
    if (!d || e->overlay!=ORION_UI_OVERLAY_CRT) return 0;
    int animated=e->refresh_bar || e->flicker || e->noise || e->horizontal_jitter || e->artifact_rate>0;
    if (!animated) return 0;

    orion_fx_surface_t sf;
    if (!fx_surface(d,&sf)) return 0;

    /* The physical Tab5 panel is native PORTRAIT (720x1280 scanout); in
     * landscape UI orientation, sf.landscape is set and the band below
     * becomes a narrow-in-x, FULL-PHYSICAL-HEIGHT (h=sf.phys_h=1280)
     * vertical stripe. sgfx_hal_dsi_flush() (lib/SGFX/src/hal/espidf/
     * espidf_dsi.c) has no strided/2D cache-sync primitive available
     * (ESP-IDF's esp_cache_msync() only takes a flat (addr,size) range) --
     * by design it syncs WHOLE contiguous rows and deliberately ignores
     * x/w narrowing, which is cheap for the portrait case (few rows) but
     * means every single landscape tick pays for an esp_cache_msync() of
     * the ENTIRE physical height (~1.8MB), regardless of the sweep band's
     * actual ~34px column width. That's a real, continuous, expensive
     * cache operation firing every ~15-30ms for as long as a CRT-themed
     * screen is open in landscape -- a plausible cause of visible
     * stutter/tearing, independent of pure flush-ordering issues.
     *
     * A proper fix would need per-row strided syncing (h separate narrow
     * esp_cache_msync() calls) in the HAL -- risky to change in a file
     * every SGFX draw call goes through without hardware profiling to
     * verify it. Minimal, safe mitigation instead: throttle how often
     * this expensive landscape flush actually fires, the same "reduce
     * redraw frequency to control cost" precedent used elsewhere in this
     * codebase (topbar badge refresh, scroll-delta throttling). Portrait's
     * cheap sync is untouched -- it already runs at full per-tick rate. */
    {
        static uint32_t s_last_land_tick_ms = 0;
        static int s_last_land_valid = 0;
        if (sf.landscape) {
            const uint32_t MIN_INTERVAL_MS = 70; /* ~14Hz, down from ~33-66Hz */
            if (s_last_land_valid && (now_ms - s_last_land_tick_ms) < MIN_INTERVAL_MS)
                return 0;
            s_last_land_tick_ms = now_ms;
            s_last_land_valid = 1;
        }
    }

    /* fx_restore_previous()'s own immediate flush is correct for every
     * OTHER caller (they draw unrelated fresh content afterward and need
     * that flush to make the restore visible at all -- see its own
     * comment). But HERE, the old band gets restored and a NEW band gets
     * drawn moments later in this SAME function, with real per-pixel
     * jitter/flicker/noise/artifact math running in between. Flushing the
     * restore on its own would commit a genuine, hardware-visible "band
     * removed" frame to the panel (DSI writes land directly in the buffer
     * being ACTIVELY, CONTINUOUSLY scanned out at ~60Hz, independent of
     * software) before the new band is even drawn -- a real strobe at the
     * tick rate. Instead, restore WITHOUT flushing, remember the old
     * band's rect, and flush exactly ONCE at the end covering the union
     * of old+new -- so the panel only ever sees last tick's or this
     * tick's FULLY-composited frame, never a partial one in between. */
    orion_ui_rect_t old_band = {0, 0, 0, 0};
    if (s_fx.valid) old_band = (orion_ui_rect_t){s_fx.x, s_fx.y, s_fx.w, s_fx.h};
    fx_restore_previous_impl(d, &sf, 0);

    int logical_h=(int)d->caps.height;
    int band=e->refresh_bar_width;
    if (band<4) band=4;
    if (band>logical_h) band=logical_h;
    int speed=e->refresh_bar_speed>0?e->refresh_bar_speed:24;
    int span=logical_h-band;
    int pos=0;
    if (span>0) {
        /* One-way sawtooth, not a ping-pong bounce.
         * A real CRT's visible rolling-picture glitch (lost vertical sync)
         * travels in ONE direction only and wraps instantly at the frame
         * boundary -- it never reverses and travels back up. The previous
         * symmetric triangle wave (down then back up) reads as a radar/
         * scanner sweep, not a CRT defect. This keeps the same per-tick
         * region-scoped restore/capture/redraw (still fully delta -- only
         * the band's own rect is ever touched) and the same jitter/
         * flicker/noise/artifact layers on top; only the position formula
         * changed, from a triangle wave to a sawtooth. */
        uint32_t travel=(uint32_t)span*1000u/(uint32_t)speed;
        if (travel<1) travel=1;
        uint32_t ph=now_ms%travel;
        pos=(int)((uint64_t)ph*(uint32_t)span/travel);
    }

    int x,y,w,h;
    if (!sf.landscape) { x=0; y=pos; w=sf.phys_w; h=band; }
    else               { x=pos; y=0; w=band; h=sf.phys_h; }
    if (!fx_capture_region(&sf,x,y,w,h)) {
        /* The restore above was deliberately left unflushed (see this
         * function's own header comment) on the assumption a new band
         * would be drawn and flushed moments later. That didn't happen --
         * flush the restored old-band rect on its own now so it doesn't
         * sit invisible-to-hardware indefinitely. */
        if (old_band.w > 0) fx_flush(d, old_band.x, old_band.y, old_band.w, old_band.h);
        return 0;
    }

    uint32_t bucket=now_ms/80u;
    uint32_t rnd=fx_hash(bucket^0x43525433u);

    int jitter=0;
    if (e->horizontal_jitter && e->jitter_amount>0) {
        int gate=e->jitter_rate>0?e->jitter_rate:3;
        if ((int)(bucket%(uint32_t)imax(1,21-gate))==0)
            jitter=(rnd&1u)?e->jitter_amount:-e->jitter_amount;
    }
    if (jitter) fx_shift_band(&sf,jitter);

    int flicker_delta=0;
    if (e->flicker && e->flicker_strength>0) {
        uint32_t f=fx_hash(bucket*(uint32_t)imax(1,e->flicker_rate)+0x77u);
        flicker_delta=(int)(f%(uint32_t)(e->flicker_strength*2+1))-e->flicker_strength;
    }
    uint16_t glow=fx_pack565(e->glow_color);

    /* Moving refresh/phosphor sweep with a triangular luminance profile. */
    for (int yy=0; yy<h; ++yy) {
        uint16_t* row=(uint16_t*)(sf.fb+(size_t)(y+yy)*sf.stride)+(size_t)x;
        for (int xx=0; xx<w; ++xx) {
            int axis=sf.landscape?xx:yy;
            int den=imax(1,band/2);
            int dist=axis<band/2?axis:(band-1-axis);
            if (dist<0) dist=0;
            int sweep=e->refresh_bar ? (3 + 10*dist/den) : 0;
            int pct=100+sweep+flicker_delta;
            uint16_t p=fx_scale565(row[xx],pct);
            if (e->refresh_bar && e->glow_strength>0)
                p=fx_mix565(p,glow,imin(18,e->glow_strength/5));
            row[xx]=p;
        }
    }

    /* Sparse moving snow, deterministic from time so host previews are
     * reproducible and firmware never depends on libc rand(). */
    if (e->noise && e->noise_density>0 && e->noise_strength>0) {
        int points=(w*h*e->noise_density)/1000;
        if (points>700) points=700;
        uint32_t seed=rnd;
        for (int i=0;i<points;++i) {
            seed=fx_hash(seed+(uint32_t)i+1u);
            int px0=(int)(seed%(uint32_t)w);
            seed=fx_hash(seed+0x9e3779b9u);
            int py0=(int)(seed%(uint32_t)h);
            uint16_t* p=(uint16_t*)(sf.fb+(size_t)(y+py0)*sf.stride)+(size_t)(x+px0);
            int delta=(seed&1u)?e->noise_strength:-e->noise_strength;
            *p=fx_scale565(*p,100+delta);
        }
    }

    /* Rare sync damage lives inside the already-backed-up moving band, so it
     * costs no second history buffer and is perfectly restored next tick. */
    if (e->artifact_rate>0 && e->artifact_strength>0 && (int)(rnd%100u)<e->artifact_rate) {
        int lines=1+(int)((rnd>>8)%3u);
        for (int i=0;i<lines;++i) {
            uint32_t r2=fx_hash(rnd+(uint32_t)i*0x12345u);
            if (!sf.landscape) {
                int ry=(int)(r2%(uint32_t)h);
                uint16_t* row=(uint16_t*)(sf.fb+(size_t)(y+ry)*sf.stride)+(size_t)x;
                int shift=1+(e->artifact_strength%imax(2,imin(12,w/12)));
                if (shift<w) memmove(row+shift,row,(size_t)(w-shift)*2u);
                for(int q=0;q<imin(shift,w);++q) row[q]=fx_mix565(row[q],glow,40);
            } else {
                int rx=(int)(r2%(uint32_t)w);
                int shift=1+e->artifact_strength%imax(2,imin(12,h/12));
                for (int yy=h-1; yy>=shift; --yy) {
                    uint16_t* dst=(uint16_t*)(sf.fb+(size_t)(y+yy)*sf.stride)+(size_t)(x+rx);
                    uint16_t* src=(uint16_t*)(sf.fb+(size_t)(y+yy-shift)*sf.stride)+(size_t)(x+rx);
                    *dst=*src;
                }
            }
        }
    }

    /* Single flush covering the union of the restored old-band rect and
     * this tick's freshly-drawn new-band rect -- see this function's own
     * header comment for why splitting this into two flush calls (one at
     * restore time, one here) caused a real, hardware-visible strobe. */
    if (old_band.w > 0) {
        int ux0 = imin(x, old_band.x), uy0 = imin(y, old_band.y);
        int ux1 = imax(x + w, old_band.x + old_band.w);
        int uy1 = imax(y + h, old_band.y + old_band.h);
        fx_flush(d, ux0, uy0, ux1 - ux0, uy1 - uy0);
    } else {
        fx_flush(d,x,y,w,h);
    }
    return 1;
}

void orion_ui_animation_tick(sgfx_device_t* d, uint32_t now_ms)
{
    if (!d) return;
    s_now_ms=now_ms;
    /* Only pay for the flushing restore (orion_ui_effects_prepare_draw(),
     * fx_restore_previous() with do_flush=1) when there's an actual reason
     * for it -- marquee content about to repaint, which needs a clean
     * baseline first. Calling it unconditionally every tick, even with no
     * marquee active, commits a real "band removed" frame to the panel
     * BEFORE orion_ui_effects_tick() below even runs, let alone draws the
     * new band: two separate flush_surface() calls per tick with nothing
     * else in between reads as the band strobing on/off. When no marquee
     * is active this tick, skip straight to orion_ui_effects_tick(), which
     * restores its own old band AND draws the new one, flushing exactly
     * once at the end -- the common case (no marquee) does one hardware
     * flush per tick, not two.
     *
     * When a marquee IS active, running orion_ui_effects_tick() FIRST
     * (instead of a separate prepare_draw+marquee+present sequence before
     * it) means the band's own update is always a single, fully-composited
     * flush, and marquee's present() is a second, ALSO fully-composited
     * flush (old band still fully valid, only marquee text changing) --
     * never a bare "removed" frame in between. The only tradeoff: if a
     * marquee's rect happens to overlap the band's rect, marquee text
     * drawn this tick could be erased by the NEXT tick's restore (its
     * backup was captured before marquee drew). Acceptable and rare --
     * marquee content and the sweep band essentially never share screen
     * space. */
    (void)orion_ui_effects_tick(d,now_ms);
    if (s_marquee_count>0) {
        sgfx_rect_t outer=d->clip;
        for (int i=0;i<s_marquee_count;++i) {
            orion_ui_marquee_slot_t* m=&s_marquee[i];
            sgfx_set_clip(d,m->clip);
            marquee_draw(d,m->r,m->text,m->fg,m->bg,m->scale,s_now_ms);
        }
        sgfx_set_clip(d,outer);
        sgfx_present(d);
    }
}
