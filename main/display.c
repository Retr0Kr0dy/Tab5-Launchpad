/*
 * display.c — Tab5 MIPI-DSI bring-up with runtime panel selection.
 *
 * ── Why this is not sgfx_autoinit() / SGFX_DRV_TAB5_AUTO ───────────────────
 * The sequence follows the board-level detection/reset behavior validated
 * against the M5Stack Tab5 reference sources and Orion's real-hardware tests.
 *
 * M5Stack ships the Tab5 with one of three different display ICs (ILI9881C,
 * ST7121, ST7123) behind the same MIPI-DSI connector, and each one needs a
 * *different* DSI lane bitrate and DPI pixel clock. Those values are consumed
 * by sgfx_hal_make_dsi() when the bus object is constructed, which happens
 * strictly before any panel driver's init() runs. sgfx_autoinit() bakes
 * exactly one SGFX_DSI_* config and one driver in at compile time, so it
 * structurally cannot pick between the three.
 *
 * SGFX_DRV_TAB5_AUTO exists as a standalone-SGFX fallback that tries each
 * panel's DCS init in turn, but the DSI backend has no register readback
 * (bus->ops->read_data is NULL), so "the write succeeded" is not evidence the
 * IC is actually present — it will resolve to the first candidate regardless
 * of what is really attached. It is deliberately not compiled into Orion
 * (main/CMakeLists.txt never defines SGFX_DRV_TAB5_AUTO).
 *
 * The reliable identity signal lives on the I2C side: SIC's Tab5 board
 * preinit() releases LCD_RST/TP_RST via the GPIO expanders and reads the touch
 * controller's firmware-version register at 0x55, caching the answer in
 * tab5_panel_detected(). sic_begin_legacy() has already done that by the time
 * app_main() calls this, so all this function does is map the answer onto the
 * right {timings, ops} pair and build the bus once, correctly.
 *
 * Timing values below are transcribed from the SGFX driver sources
 * (lib/SGFX/src/drivers/{ili9881c,st7121,st7123}.c file headers), which took
 * them from M5Stack's actually-invoked Tab5 bring-up code rather than from the
 * generic per-panel component headers' unused template defaults.
 *
 * ── Two SGFX integration gaps worked around here ───────────────────────────
 * These remain Orion-side integration wrappers instead of firmware-specific
 * behavior being added to SGFX's reusable public API. Both were found during
 * Tab5 bring-up:
 *
 *   1. sgfx_open_dsi() never populates dev->caps / dev->scratch — it memsets
 *      the device and sets only bus+drv, leaving the clip box 0x0 and every
 *      draw call a silent no-op. Worked around by not calling it: this file
 *      does sgfx_hal_make_dsi() + sgfx_init() itself, which is the entry
 *      point that sets all of that up.
 *   2. All three DSI panel drivers leave fill_rect NULL, because a video-mode
 *      DSI panel has no CASET/RASET windowing — they expose get_fb_ptr /
 *      flush_surface instead, for sgfx_present_frame()'s tile-diffing path.
 *      Immediate-mode drawing (sgfx_clear / sgfx_fill_rect /
 *      sgfx_text5x7_scaled straight at the device, which is all Orion's
 *      splash and touch menu need) would therefore return SGFX_ERR_NOSUP.
 *      Worked around by wrapping the selected panel's ops table with two
 *      members of our own — a fill_rect that packs RGB565 straight into the
 *      scanout buffer while accumulating a dirty box, and a present that
 *      cache-writes-back just that box. Everything else is copied verbatim
 *      from the panel driver.
 *
 * ── Backlight ──────────────────────────────────────────────────────────────
 * Also handled here. The SGFX panel drivers deliberately leave .brightness
 * NULL with a comment
 * saying it belongs at the app layer). Without it the panel initialises and
 * scans out but the screen stays dark, which makes every display test
 * unfalsifiable — so Orion drives it: LEDC low-speed, GPIO22, 12-bit, 5 kHz,
 * duty = 4095 * pct / 100, exactly matching bsp_display_brightness_init() in
 * M5Tab5-UserDemo/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c.
 *
 * Timer/channel choice matters: the reference uses LEDC_TIMER_0 +
 * LEDC_CHANNEL_1 here and LEDC_TIMER_0 + LEDC_CHANNEL_0 for the camera XCLK —
 * whichever initialises second silently destroys the other's output. SIC's
 * camera driver already moved XCLK to LEDC_TIMER_1 (keeping CHANNEL_0), so
 * keeping the reference's TIMER_0 + CHANNEL_1 for the backlight is now safe
 * and both can run at once.
 */

#include "orion.h"
#include "ui_theme.h"
#include "studio_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/ledc.h"

#include "sgfx_hal.h"

#include "sic/sic.h"
#include "boards/tab5/panel_detect.h"

/* ── Panel driver symbols (all three compiled; one selected at runtime) ──── */
extern const sgfx_driver_ops_t sgfx_ili9881c_ops;
extern const sgfx_caps_t       sgfx_ili9881c_caps_default;
extern const sgfx_driver_ops_t sgfx_st7121_ops;
extern const sgfx_caps_t       sgfx_st7121_caps_default;
extern const sgfx_driver_ops_t sgfx_st7123_ops;
extern const sgfx_caps_t       sgfx_st7123_caps_default;

/* ── Backlight (LEDC on GPIO22) ─────────────────────────────────────────── */
#define ORION_BL_GPIO     22
#define ORION_BL_MODE     LEDC_LOW_SPEED_MODE
#define ORION_BL_TIMER    LEDC_TIMER_0
#define ORION_BL_CHANNEL  LEDC_CHANNEL_1
#define ORION_BL_RES      LEDC_TIMER_12_BIT
#define ORION_BL_MAX_DUTY 4095
#define ORION_BL_FREQ_HZ  5000

static int s_bl_pct  = -1;
static int s_bl_init = 0;

static int orion_backlight_init(void)
{
    if (s_bl_init) return 0;

    ledc_timer_config_t tcfg = {
        .speed_mode      = ORION_BL_MODE,
        .duty_resolution = ORION_BL_RES,
        .timer_num       = ORION_BL_TIMER,
        .freq_hz         = ORION_BL_FREQ_HZ,
        /* Must match the camera driver's explicit LEDC_USE_PLL_DIV_CLK
         * (SIC/src/backends/espidf/sic_espidf_camera.c) exactly, not
         * LEDC_AUTO_CLK -- ESP32-P4 has one shared global LEDC slow clock
         * across every low-speed timer (no per-timer mux), so two timers
         * independently auto-selecting a source race and whichever inits
         * second fails with "timer clock conflict". Confirmed on real
         * hardware: this was the root cause of the camera's rc=-5 start
         * failure whenever backlight init ran first.
         */
        .clk_cfg         = LEDC_USE_PLL_DIV_CLK,
    };
    if (ledc_timer_config(&tcfg) != ESP_OK) return -1;

    ledc_channel_config_t ccfg = {
        .gpio_num   = ORION_BL_GPIO,
        .speed_mode = ORION_BL_MODE,
        .channel    = ORION_BL_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = ORION_BL_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    if (ledc_channel_config(&ccfg) != ESP_OK) return -1;

    s_bl_init = 1;
    s_bl_pct  = 0;
    return 0;
}

/* Raw duty write, bypassing both the percent floor below and the gamma
 * curve -- shared by orion_backlight_set() and orion_backlight_off_full().
 * Does NOT touch s_bl_pct (callers decide what that should read). */
static int orion_backlight_write_duty(uint32_t duty)
{
    if (!s_bl_init && orion_backlight_init() != 0) return -1;
    if (duty > ORION_BL_MAX_DUTY) duty = ORION_BL_MAX_DUTY;
    if (ledc_set_duty(ORION_BL_MODE, ORION_BL_CHANNEL, duty) != ESP_OK) return -1;
    if (ledc_update_duty(ORION_BL_MODE, ORION_BL_CHANNEL) != ESP_OK) return -1;
    return 0;
}

int orion_backlight_set(int percent)
{
    /* 0% would drive the backlight fully off (duty=0), functionally a
     * black screen with nothing distinguishing "very dim" from "can't see
     * to drag back up". Floored at 1% instead of 0 so the panel is always
     * at least faintly lit and recoverable by touch alone. This is the
     * normal user-facing slider path; a real, deliberate full-off (no
     * floor) is orion_backlight_off_full() below, used only by idle sleep,
     * where "can't see to drag back up" doesn't apply since sleep wakes on
     * any touch, blind, not a drag gesture. */
    if (percent < 1)   percent = 1;
    if (percent > 100) percent = 100;
    /* A LINEAR percent->duty map is not a linear percent->PERCEIVED-
     * BRIGHTNESS map: LED/backlight luminous output and human brightness
     * perception are both non-linear (roughly power-law/Weber-Fechner), so
     * a linear duty curve spends most of its useful visual range bunched
     * in the first few percent and looks "already bright" well before the
     * duty cycle is actually high, leaving the low end of the slider both
     * not dim enough and not granular enough. A standard perceptual gamma
     * correction (2.2, the same constant displays/LEDs are conventionally
     * gamma-corrected with) compresses low PERCENT values down to
     * genuinely low DUTY values while still reaching full duty at 100%,
     * spreading the perceptually-relevant low range across more of the
     * 1-100 slider instead of collapsing it into the first few percent. */
    double frac = pow((double)percent / 100.0, 2.2);
    uint32_t duty = (uint32_t)(ORION_BL_MAX_DUTY * frac + 0.5);
    /* At percent=1 the gamma curve above rounds to duty=0, silently
     * defeating the floor this function exists to guarantee. The floor is
     * about the DUTY actually reaching the panel, not the percent number,
     * so it must be re-applied here, after the curve, not just on the
     * input. */
    if (duty < 1) duty = 1;
    if (orion_backlight_write_duty(duty) != 0) return -1;
    s_bl_pct = percent;
    return 0;
}

int orion_backlight_get(void) { return s_bl_pct; }

/* True full-off (duty=0, no 1% floor), for real
 * idle sleep -- see orion_backlight_set()'s own comment for why the floor
 * exists and why it doesn't apply here (sleep wakes on any blind touch,
 * not a "see the slider to drag it" gesture). Does not update s_bl_pct,
 * so orion_backlight_get() still reports the real percent to restore on
 * wake -- callers save that themselves and call orion_backlight_set() with
 * it directly, this function is deliberately a one-way "go dark" primitive
 * only. */
int orion_backlight_off_full(void)
{
    return orion_backlight_write_duty(0);
}

/* ── Direct-framebuffer draw shim (see file header, gap #2) ─────────────── */

#define ORION_SCRATCH_BYTES 4096

static sgfx_device_t     s_dev;
static uint8_t           s_scratch[ORION_SCRATCH_BYTES];
static sgfx_driver_ops_t s_ops;              /* panel ops + our two members */
static uint8_t*          s_fb;               /* hardware scanout buffer     */
static size_t            s_stride;           /* its row pitch, in bytes     */
static int               s_ready;
static const char*       s_panel_name = "none";
static int               s_w, s_h;

static int s_dx0, s_dy0, s_dx1, s_dy1;       /* dirty box, x1/y1 exclusive */

/* ── Rotation ─────────────────────────────────────────────────────────── */
static int s_rotation;                       /* 0/1/2/3 -- see orion.h */
static int s_auto_rotation = 1;

#define ORION_ROT_STACK_MAX 4
static int s_rot_stack[ORION_ROT_STACK_MAX];
static int s_rot_stack_n;

/* d->caps.width/height double as SGFX's own clip bounds (sgfx_reset_clip()
 * derives d->clip from them) -- keeping them in sync with the current
 * rotation is what makes gfx_core.c's own clipping in sgfx_fill_rect()
 * correctly bound logical (not physical) coordinates, so a landscape screen
 * laying out up to orion_display_w()==1280 doesn't get silently clipped
 * against the physical panel's fixed 720px width. */
static void apply_rotation_to_caps(void)
{
    int lw = (s_rotation & 1) ? s_h : s_w;
    int lh = (s_rotation & 1) ? s_w : s_h;
    s_dev.caps.width  = (uint16_t)lw;
    s_dev.caps.height = (uint16_t)lh;
    sgfx_reset_clip(&s_dev);
}

void orion_display_set_rotation(int rot)
{
    rot &= 3;
    if (rot == s_rotation) return;
    s_rotation = rot;
    if (s_ready) apply_rotation_to_caps();
}

int orion_display_get_rotation(void) { return s_rotation; }
void orion_display_set_auto_rotation(int enabled) { s_auto_rotation = enabled ? 1 : 0; }
int orion_display_auto_rotation_enabled(void) { return s_auto_rotation; }

void orion_display_push_rotation(int rot)
{
    if (s_rot_stack_n < ORION_ROT_STACK_MAX) s_rot_stack[s_rot_stack_n++] = s_rotation;
    orion_display_set_rotation(rot);
}

void orion_display_pop_rotation(void)
{
    if (s_rot_stack_n > 0) orion_display_set_rotation(s_rot_stack[--s_rot_stack_n]);
}

int orion_display_phys_w(void) { return s_w; }
int orion_display_phys_h(void) { return s_h; }

/* Logical (x,y,w,h) -> physical (px,py,pw,ph), both axis-aligned rects (a
 * 90°-multiple rotation of a rectangle is still a rectangle, just with w/h
 * swapped and repositioned -- no per-pixel reshaping needed, which is why a
 * single transform here correctly covers everything from single pixels to
 * whole-glyph text cells to full-screen clears). s_w/s_h are the FIXED
 * physical panel dims (720x1280), never swapped. */
void orion_display_logical_to_phys(int lx, int ly, int lw, int lh,
                                   int* px, int* py, int* pw, int* ph)
{
    switch (s_rotation) {
    case 1: /* 90 CW: logical canvas is s_h x s_w */
        *px = s_w - ly - lh; *py = lx; *pw = lh; *ph = lw;
        break;
    case 2: /* 180 */
        *px = s_w - lx - lw; *py = s_h - ly - lh; *pw = lw; *ph = lh;
        break;
    case 3: /* 270 CW (90 CCW): logical canvas is s_h x s_w */
        *px = ly; *py = s_h - lx - lw; *pw = lh; *ph = lw;
        break;
    default: /* 0 */
        *px = lx; *py = ly; *pw = lw; *ph = lh;
        break;
    }
}

/* Inverse of the point case above (lw=lh=0) -- physical -> logical, needed
 * for touch input: the real touch driver reports physical panel
 * coordinates, but every screen's hit-testing compares against logical
 * layout math (orion_display_w()/h()). Derived by solving each branch above
 * for (lx,ly) given (px,py); rot2 is self-inverse (180° twice is identity). */
void orion_display_phys_to_logical(int px, int py, int* lx, int* ly)
{
    switch (s_rotation) {
    case 1: *lx = py;         *ly = s_w - px; break;
    case 2: *lx = s_w - px;   *ly = s_h - py; break;
    case 3: *lx = s_h - py;   *ly = px;       break;
    default: *lx = px;        *ly = py;       break;
    }
}

/* IMU-driven auto-rotation. Reuses the exact axis mapping and debounce
 * window already confirmed correct on real hardware for video_player.c's
 * independent content-rotation feature (vp_orientation_from_imu()) -- same
 * underlying question ("which way is up, given how gravity is pulling on
 * the device right now"), so the same answer applies; kept as a separate
 * small copy rather than a cross-file call since that function is
 * video_player.c-private and returns its own enum type, not worth coupling
 * two files for ~15 lines. az dominating (device lying flat) means "no
 * signal" and keeps the last rotation, same reasoning as that function. */
#define ORION_ROT_DEBOUNCE_MS 300

void orion_display_poll_auto_rotation(void)
{
    if (!s_auto_rotation) return;
    static int      s_candidate;
    static uint32_t s_candidate_since;

    /* A pinned rotation (orion_display_push_rotation(), e.g. the IMU test
     * screen forcing portrait) must not be fought by auto-detect -- push/
     * pop only save/restore the value, they don't block writes in between,
     * so this function is the one that has to respect the pin. Any screen
     * that calls this polling function (directly or via a shared helper
     * like wait_for_dismiss()) while a rotation is pinned would otherwise
     * silently un-pin it. */
    if (s_rot_stack_n > 0) return;

    const imu_t* imu = sic_imu(0);
    if (!imu || !imu->v) return;
    sic_imu_sample_t s;
    if (imu->v->read(imu, &s) != 0) return;
    if (fabsf(s.az) > fabsf(s.ax) && fabsf(s.az) > fabsf(s.ay)) return; /* flat */

    int sample;
    if (fabsf(s.ax) > fabsf(s.ay)) {
        sample = (s.ax > 0) ? 3 : 1;
    } else {
        sample = (s.ay > 0) ? 0 : 2;
    }

    uint32_t now = orion_millis();
    if (sample != s_candidate) {
        s_candidate = sample;
        s_candidate_since = now;
    }
    if (sample == s_rotation) return;
    if (now - s_candidate_since >= ORION_ROT_DEBOUNCE_MS) {
        orion_display_set_rotation(s_candidate);
    }
}

static void dirty_reset(void)
{
    s_dx0 = s_dy0 = 0x7fffffff;
    s_dx1 = s_dy1 = 0;
}

static void dirty_add(int x, int y, int w, int h)
{
    if (x < s_dx0) s_dx0 = x;
    if (y < s_dy0) s_dy0 = y;
    if (x + w > s_dx1) s_dx1 = x + w;
    if (y + h > s_dy1) s_dy1 = y + h;
}

static int orion_dsi_fill_rect(sgfx_device_t* d, int x, int y, int w, int h,
                               sgfx_rgba8_t c)
{
    (void)d;
    if (!s_fb) return SGFX_ERR_NOSUP;
    /* sgfx_fill_rect() has already clipped (x,y,w,h) to the device's LOGICAL
     * clip box (d->caps.width/height, kept in sync with rotation by
     * apply_rotation_to_caps()). Transform into physical scanout coordinates
     * before writing -- the real buffer is always s_w x s_h regardless of
     * logical rotation. This one transform is what every draw primitive in
     * SGFX (fill/clear/rect/hline/vline/text, since text is built from many
     * small fill_rect calls per glyph cell) automatically inherits. */
    int px, py, pw, ph;
    orion_display_logical_to_phys(x, y, w, h, &px, &py, &pw, &ph);

    uint16_t v = (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
    for (int j = 0; j < ph; ++j) {
        uint16_t* row = (uint16_t*)(s_fb + (size_t)(py + j) * s_stride) + px;
        for (int i = 0; i < pw; ++i) row[i] = v;
    }
    dirty_add(px, py, pw, ph);
    return SGFX_OK;
}

/* ── set_window / write_pixels: real pixel-buffer blit support ──────────
 *
 * `sgfx_blit()` (gfx_core.c) needs both of these; this panel driver left
 * them NULL for the same reason `fill_rect` needed its own shim above
 * (video-mode DSI has no CASET/RASET windowing) -- so `sgfx_blit()`
 * currently returns SGFX_ERR_NOSUP on real hardware. This is what makes
 * it work, so `su_text()` can compose a glyph into a local buffer and
 * blit it in one shot instead of one `fill_rect()` call per antialiasing
 * run (see studio_ui.c and CLAUDE.md's Rendering section for why that
 * call-count mattered).
 *
 * `sgfx_blit()`'s contract: ONE set_window(x,y,w,h) in LOGICAL, clipped
 * coordinates, then exactly `h` calls to write_pixels(), each delivering
 * one LOGICAL row's worth of `w` pixels, top row first. set_window stores
 * the logical->physical transform (reusing the exact same, already-proven
 * orion_display_logical_to_phys() this file's fill_rect already depends
 * on) and a row counter; write_pixels uses that counter to know which
 * logical row this call represents.
 *
 * The real complexity: for a 90°/270° rotation, logical_to_phys SWAPS
 * width and height (a rect transposes) -- so per pixel, one LOGICAL ROW
 * (fixed logical y, varying logical x) lands along one PHYSICAL COLUMN
 * (fixed physical x, varying physical y), not a contiguous physical row.
 * Each case below is the per-pixel specialization of logical_to_phys's
 * own four cases, derived by substituting a 1x1 rect at (x+i, y+j) into
 * that function's existing formulas and solving for how physical x/y vary
 * with i (position within the row) and j (which row number this call is)
 * -- not new/independent transform logic, the same one generalized from
 * whole-rects to per-pixel. Only rotations 1/3 are reachable from the
 * Launchpad screen itself (orion_display_push_rotation() forces one of
 * those two), but su_text() is shared with Settings/Diagnostics, which
 * run at rotation 0 too, so all four must be correct. NOT exercised by
 * the native preview tool (its host SGFX backend has its own, unrotated
 * set_window/write_pixels -- this rotation math only runs on real
 * hardware), so this needs real on-device visual verification, not just
 * a clean native-preview pixel-diff. */
static int s_win_px, s_win_py, s_win_pw, s_win_ph; /* physical window, set by set_window */
static int s_win_lw;                               /* logical width == pixels per write_pixels() call */
static int s_win_row;                               /* which logical row (0-based) the next write_pixels() call is */

static int orion_dsi_set_window(sgfx_device_t* d, int x, int y, int w, int h)
{
    (void)d;
    if (!s_fb) return SGFX_ERR_NOSUP;
    orion_display_logical_to_phys(x, y, w, h, &s_win_px, &s_win_py, &s_win_pw, &s_win_ph);
    s_win_lw = w;
    s_win_row = 0;
    dirty_add(s_win_px, s_win_py, s_win_pw, s_win_ph);
    return SGFX_OK;
}

static int orion_dsi_write_pixels(sgfx_device_t* d, const void* px, size_t count, sgfx_pixfmt_t fmt)
{
    (void)d;
    if (!s_fb) return SGFX_ERR_NOSUP;
    if (fmt != SGFX_FMT_RGB565) return SGFX_ERR_NOSUP; /* the only format su_text() ever composes */
    int j = s_win_row++;
    const uint16_t* src = (const uint16_t*)px;
    int n = (int)count;
    if (n > s_win_lw) n = s_win_lw;

    switch (s_rotation) {
    case 1: { /* 90 CW: one logical row -> one physical COLUMN, decreasing physical x per row */
        int phys_x = s_win_px + s_win_pw - 1 - j;
        if ((unsigned)phys_x >= (unsigned)s_w) break;
        for (int i = 0; i < n; ++i) {
            int phys_y = s_win_py + i;
            if ((unsigned)phys_y >= (unsigned)s_h) continue;
            *((uint16_t*)(s_fb + (size_t)phys_y * s_stride) + phys_x) = src[i];
        }
        break;
    }
    case 2: { /* 180: one logical row -> one physical row, written right-to-left */
        int phys_y = s_win_py + s_win_ph - 1 - j;
        if ((unsigned)phys_y >= (unsigned)s_h) break;
        uint16_t* row = (uint16_t*)(s_fb + (size_t)phys_y * s_stride);
        int base_x = s_win_px + s_win_pw - 1;
        for (int i = 0; i < n; ++i) {
            int phys_x = base_x - i;
            if ((unsigned)phys_x >= (unsigned)s_w) continue;
            row[phys_x] = src[i];
        }
        break;
    }
    case 3: { /* 270 CW: one logical row -> one physical COLUMN, increasing physical x per row */
        int phys_x = s_win_px + j;
        if ((unsigned)phys_x >= (unsigned)s_w) break;
        int base_y = s_win_py + s_win_ph - 1;
        for (int i = 0; i < n; ++i) {
            int phys_y = base_y - i;
            if ((unsigned)phys_y >= (unsigned)s_h) continue;
            *((uint16_t*)(s_fb + (size_t)phys_y * s_stride) + phys_x) = src[i];
        }
        break;
    }
    default: { /* 0: one logical row -> one physical row, contiguous */
        int phys_y = s_win_py + j;
        if ((unsigned)phys_y >= (unsigned)s_h) break;
        uint16_t* row = (uint16_t*)(s_fb + (size_t)phys_y * s_stride) + s_win_px;
        for (int i = 0; i < n; ++i) row[i] = src[i];
        break;
    }
    }
    return SGFX_OK;
}

/* What's actually on screen right now (the buffer most recently flipped
 * to) -- separate from s_fb, which is the buffer CURRENTLY being drawn
 * into (the back buffer, not yet visible). orion_fb_ptr()'s diagnostic/
 * screenshot consumers want this one, not s_fb -- see its own comment. */
static uint8_t* s_front_fb;

static int orion_dsi_present(sgfx_device_t* d)
{
    (void)d;
    if (s_dx1 <= s_dx0 || s_dy1 <= s_dy0) {
#if ORION_ENABLE_PERF_DIAGNOSTICS
        printf("[GFX-DBG] present: nothing dirty\n");
#endif
        return SGFX_OK; /* clean */
    }
    /* Real double-buffering: flip the buffer we just drew into (s_fb) to
     * become the new front/scanned buffer, synced to the panel's own
     * vsync (sgfx_hal_dsi_flip() -> dsi_on_refresh_done's semaphore) --
     * atomic from the panel's point of view, unlike the single-buffer
     * cache-flush-into-the-live-buffer approach this replaced, which
     * could tear whenever a write landed mid-scan. flip() also performs
     * its own full-buffer cache writeback internally, so the dirty box
     * above is only used as a "was anything drawn at all" gate now, not
     * a fine-grained flush region -- every screen in this firmware does
     * a full redraw whenever it redraws at all (see launchpad_app.c's
     * frame-pacing comment), so there is no finer-grained region to
     * flush anyway. */
    int rc = sgfx_hal_dsi_flip(s_dev.bus, 200);
#if ORION_ENABLE_PERF_DIAGNOSTICS
    printf("[GFX-DBG] present: dirty box (%d,%d)-(%d,%d) flip rc=%d\n",
           s_dx0, s_dy0, s_dx1, s_dy1, rc);
#endif
    s_front_fb = s_fb;
    s_fb = (uint8_t*)sgfx_hal_dsi_get_back_fb(s_dev.bus, &s_stride);
#if ORION_ENABLE_PERF_DIAGNOSTICS
    printf("[GFX-DBG] present: flush_surface rc=%d\n", rc);
    /* Peek at the first few pixels we just wrote, straight from s_fb, so we
     * can tell a drawing bug from a scanout/cache bug: if these are non-zero
     * but the screen is still black, the bug is downstream of the CPU write. */
    if (s_fb) {
        uint16_t* p = (uint16_t*)(s_fb + (size_t)s_dy0 * s_stride) + s_dx0;
        printf("[GFX-DBG] fb[%d,%d..]: %04x %04x %04x %04x\n",
               s_dx0, s_dy0, p[0], p[1], p[2], p[3]);
        /* Scan a row through where the "TAB5-ORION" title (scale-4 text,
         * starting at x=16,y=40) should be, and report how many pixels in
         * that row differ from the background fill - tells us whether text
         * rendering happened at all, separate from the background fill. */
        if (s_dy0 <= 44 && s_dy1 > 44) {
            uint16_t bg = p[0];
            uint16_t* row = (uint16_t*)(s_fb + (size_t)44 * s_stride);
            int diff = 0, first_x = -1;
            for (int x = 0; x < 400; ++x) {
                if (row[x] != bg) { diff++; if (first_x < 0) first_x = x; }
            }
            printf("[GFX-DBG] row y=44 x[0..400): %d px differ from bg=%04x"
                   " (first diff at x=%d)\n", diff, bg, first_x);
        }
    }
#endif
    dirty_reset();
    return rc;
}

/* ── Bring-up ──────────────────────────────────────────────────────────── */

int orion_display_init(struct konsole* ks)
{
    sgfx_hal_cfg_dsi_t cfg;
    const sgfx_driver_ops_t* ops;
    const sgfx_caps_t*       caps;

    /* Fields common to all three panels. Reset is deliberately -1: LCD_RST
     * hangs off a PI4IOE5V6408 expander bit and was already released by SIC's
     * tab5_ioexp_init(), so SGFX must not drive a GPIO for it. */
    cfg.lane_count = 2;
    cfg.width      = 720;
    cfg.height     = 1280;
    cfg.fb_fmt     = SGFX_FMT_RGB565;
    cfg.pin_rst    = -1;
    cfg.ldo_chan   = 3;
    cfg.ldo_mv     = 2500;
    /* Real double-buffering for ALL drawing, not just the (unused in this
     * firmware -- no video player here) PPA fast path: orion_dsi_fill_rect()/
     * orion_dsi_present() below draw into the back buffer and flip on
     * present (sgfx_hal_dsi_get_back_fb()/sgfx_hal_dsi_flip()), so every
     * screen gets a tear-free, vsync-synced update for free with no
     * per-screen code of its own. See orion_dsi_present()'s comment for
     * the mechanism. */
    cfg.num_fbs    = 2;

    tab5_panel_kind_t kind = tab5_panel_detected();
    if (kind == TAB5_PANEL_UNKNOWN) {
        /* Either SIC's preinit did not run, or the 0x55 probe found nothing.
         * Carry on with the most common variant rather than leaving the screen
         * dark: a mismatched DSI config shows a garbled/blank panel, which is
         * diagnosable, and is no worse than not initialising at all. */
        kon_printf(ks, "[GFX] panel UNKNOWN (SIC detect did not run or found "
                       "nothing) - assuming ILI9881C\r\n");
        kind = TAB5_PANEL_ILI9881C_GT911;
    }

    switch (kind) {
    case TAB5_PANEL_ST7121:
        s_panel_name = "ST7121";
        ops  = &sgfx_st7121_ops;  caps = &sgfx_st7121_caps_default;
        cfg.lane_mbps = 965; cfg.dpi_clk_hz = 70000000;
        cfg.hsync =  2; cfg.hbp =  40; cfg.hfp =  40;
        cfg.vsync = 20; cfg.vbp =  24; cfg.vfp = 200;
        break;
    case TAB5_PANEL_ST7123:
        s_panel_name = "ST7123";
        ops  = &sgfx_st7123_ops;  caps = &sgfx_st7123_caps_default;
        cfg.lane_mbps = 965; cfg.dpi_clk_hz = 70000000;
        cfg.hsync =  2; cfg.hbp =  40; cfg.hfp =  40;
        cfg.vsync =  2; cfg.vbp =   8; cfg.vfp = 220;
        break;
    case TAB5_PANEL_ILI9881C_GT911:
    default:
        s_panel_name = "ILI9881C";
        ops  = &sgfx_ili9881c_ops; caps = &sgfx_ili9881c_caps_default;
        cfg.lane_mbps = 730; cfg.dpi_clk_hz = 60000000;
        cfg.hsync = 40; cfg.hbp = 140; cfg.hfp =  40;
        cfg.vsync =  4; cfg.vbp =  20; cfg.vfp =  20;
        break;
    }

    kon_printf(ks, "[GFX] DSI panel=%s lanes=%d %uMbps dpi=%uHz\r\n",
               s_panel_name, cfg.lane_count,
               (unsigned)cfg.lane_mbps, (unsigned)cfg.dpi_clk_hz);

    /* Heap-allocated because sgfx_device_t keeps the pointer: it must outlive
     * this frame. Same as sgfx_open_dsi()/sgfx_autoinit() do internally. */
    sgfx_bus_t* bus = (sgfx_bus_t*)calloc(1, sizeof(sgfx_bus_t));
    if (!bus) { kon_printf(ks, "[GFX] bus alloc failed\r\n"); return -1; }

    int rc = sgfx_hal_make_dsi(bus, &cfg);
    if (rc < 0) {
        kon_printf(ks, "[GFX] make_dsi rc=%d\r\n", rc);
        free(bus);
        return rc;
    }

    /* DIAGNOSTIC: confirms the low-speed DSI command channel is
     * actually being understood by the panel, not just "not erroring out" at
     * the ESP-IDF transport level. Same ID-register read the real esp_lcd_ili9881c
     * component does at the top of its own init(). ILI9881C-only for now
     * (matches the panel actually detected on this unit); harmless to call
     * before any DCS init table runs regardless of which panel is selected,
     * since it only touches Page-1 registers no panel's init table depends on
     * being in a particular prior state. */
#if ORION_ENABLE_PERF_DIAGNOSTICS
    if (kind == TAB5_PANEL_ILI9881C_GT911) {
        uint8_t id1, id2, id3;
        int idrc = sgfx_hal_dsi_debug_read_ili9881c_id(bus, &id1, &id2, &id3);
        kon_printf(ks, "[GFX-DBG] ILI9881C ID read rc=%d: %02x %02x %02x\r\n",
                   idrc, id1, id2, id3);
    }
#endif

    s_ops = *ops;
    s_ops.fill_rect    = orion_dsi_fill_rect;
    s_ops.set_window   = orion_dsi_set_window;
    s_ops.write_pixels = orion_dsi_write_pixels;
    s_ops.present      = orion_dsi_present;
    dirty_reset();

    rc = sgfx_init(&s_dev, bus, &s_ops, caps, s_scratch, sizeof s_scratch);
    if (rc) {
        kon_printf(ks, "[GFX] %s init rc=%d\r\n", s_panel_name, rc);
        free(bus);
        return rc;
    }
    /* Video (continuous scanout of buffer 0) was already started inside
     * dev->drv->init() just above (e.g. st7121_init() calls
     * sgfx_hal_dsi_start_video() itself) -- buffer 0 is scanning out
     * whatever garbage/zeroed content it has until the first
     * orion_dsi_present() below flips to the buffer we actually drew
     * into. */
    s_w = s_dev.caps.width;
    s_h = s_dev.caps.height;

    /* Draw into the BACK buffer from the very first frame (not
     * s_ops.get_fb_ptr(), which always returns buffer 0 regardless of
     * scanout state -- see orion_dsi_present()'s own comment on why this
     * project draws into the back buffer and flips, instead). Video
     * hasn't started yet (sgfx_init() -> the panel driver's init() does
     * that next), so nothing is being scanned out of either buffer right
     * now; whichever one sgfx_hal_dsi_get_back_fb() calls "back" at this
     * point is as good a first draw target as the other. */
    if (!(s_fb = (uint8_t*)sgfx_hal_dsi_get_back_fb(bus, &s_stride))) {
        kon_printf(ks, "[GFX] scanout buffer unavailable\r\n");
        return -1;
    }
    if (s_stride == 0) s_stride = (size_t)s_w * 2u;

    kon_printf(ks, "[GFX] %s ok %dx%d fb=%p stride=%u\r\n",
               s_panel_name, s_w, s_h, (void*)s_fb, (unsigned)s_stride);

    s_ready = 1;

    sgfx_clear(&s_dev, (sgfx_rgba8_t){0, 0, 0, 255});
    sgfx_present(&s_dev);

    if (orion_backlight_set(50) != 0)
        kon_printf(ks, "[GFX] backlight init failed (GPIO%d)\r\n", ORION_BL_GPIO);
    else
        kon_printf(ks, "[GFX] backlight on (GPIO%d, %d%%)\r\n",
                   ORION_BL_GPIO, s_bl_pct);

    return 0;
}

int            orion_display_ready(void) { return s_ready; }
const char*    orion_panel_name(void)    { return s_panel_name; }
sgfx_device_t* orion_gfx(void)           { return s_ready ? &s_dev : NULL; }
/* Rotation-aware logical dims (see apply_rotation_to_caps()) -- swapped for
 * 90°/270°. Everything in touchmenu.c already lays out against these rather
 * than hardcoded 720/1280, so this one change is what makes every existing
 * screen's layout math adapt to landscape automatically. */
int            orion_display_w(void)     { return s_dev.caps.width; }
int            orion_display_h(void)     { return s_dev.caps.height; }

/* Returns what's actually ON SCREEN right now (s_front_fb), not the back
 * buffer currently being drawn into (s_fb) -- a screenshot/diagnostic
 * reading the draw-in-progress buffer would show whatever was left there
 * two frames ago (stale or uninitialized), not the real current display
 * content. Falls back to s_fb only in the narrow window before the very
 * first orion_dsi_present() call has ever run (s_front_fb still NULL). */
void* orion_fb_ptr(size_t* stride_bytes)
{
    if (stride_bytes) *stride_bytes = s_stride;
    return s_front_fb ? s_front_fb : s_fb;
}

/* Real double-buffering, for the video player's zero-copy
 * PPA-to-framebuffer path -- see sgfx_hal_dsi_get_back_fb()/sgfx_hal_dsi_flip()
 * (SGFX/include/sgfx_hal.h) for the actual mechanism and the "Ownership
 * rule" (only one thing may write frames while it holds the back buffer;
 * normal SGFX drawing via orion_fb_ptr() above is unaffected either way,
 * but the caller flipping away from buffer 0 and never flipping back would
 * leave menu/console drawing invisible until it did). */
void* orion_video_get_back_fb(size_t* stride_bytes)
{
    if (!s_ready) return NULL;
    return sgfx_hal_dsi_get_back_fb(s_dev.bus, stride_bytes);
}

int orion_video_flip(uint32_t timeout_ms)
{
    if (!s_ready) return SGFX_ERR_INVAL;
    return sgfx_hal_dsi_flip(s_dev.bus, timeout_ms);
}

int orion_video_flip_hw_written(uint32_t timeout_ms)
{
    if (!s_ready) return SGFX_ERR_INVAL;
    return sgfx_hal_dsi_flip_hw_written(s_dev.bus, timeout_ms);
}

int orion_video_flip_to_front(uint32_t timeout_ms)
{
    if (!s_ready) return SGFX_ERR_INVAL;
    return sgfx_hal_dsi_flip_to_front(s_dev.bus, timeout_ms);
}

/* ── Boot splash ───────────────────────────────────────────────────────── */

void orion_display_splash(void)
{
    if (!s_ready) return;
    su_splash_draw(&s_dev,s_w,s_h,ORION_VERSION);
    sgfx_present(&s_dev);
}
