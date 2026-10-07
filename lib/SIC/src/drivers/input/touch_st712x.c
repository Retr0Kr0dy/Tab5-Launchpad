/*
 * touch_st712x.c — ST7121 / ST7123 combined display+touch controller,
 * touch side only (I2C address 0x55, shared with the display identity
 * register probed once in boards/tab5/panel_detect.c).
 *
 * ST7121 and ST7123 share the exact same touch register layout on Tab5
 * (only the *display* driver differs between the two panel ICs), so both
 * hints are registered from this one file.
 *
 * Register map and touch-report byte layout are mined (protocol only, no
 * code copied) from Espressif's published `esp_lcd_touch_st7123` component
 * (esp-iot-solution, components/display/lcd_touch/esp_lcd_touch_st7123/),
 * the same family this exact chip's official ESP-IDF touch driver belongs
 * to (M5Tab5-UserDemo's own idf_component.yml pulls this component in as
 * `espressif/esp_lcd_touch_st7123` — see m5stack_tab5/idf_component.yml —
 * but the managed-component source itself is not vendored into the
 * checked-out reference tree, so it was fetched from its public GitHub
 * source for register-map mining rather than guessed):
 *
 *   0x0000  FW_VERSION_REG   — 1 byte. Already used by panel_detect.c to
 *                              distinguish ST7121 (value 1) from ST7123
 *                              (value 3); this driver does not re-read it.
 *   0x0009  MAX_TOUCHES_REG  — 1 byte, number of touch report slots to
 *                              read this poll. Re-read every poll (not
 *                              cached at init) per the mined reference.
 *   0x0010  Advanced-info status byte, bitfields (LSB-first packing):
 *             bits[1:0] reserved
 *             bit 2     with_prox
 *             bit 3     with_coord  — 1 = new coordinate data is ready
 *             bits[6:4] prox_status
 *             bit 7     rst_chip
 *           Only bit 3 (with_coord) is consulted here; if clear, that is
 *           a normal "no new touch data" polling outcome, not an error.
 *   0x0014  REPORT_COORD_0_REG — first touch-report record. Report N is
 *           at 0x0014 + N*7 (7 bytes/record — see struct layout note
 *           below), read in one burst covering all requested records.
 *
 * Touch-report record (7 bytes), reconstructed from the mined struct's
 * bitfield names (particularly `reserved_49_55`, whose name encodes its
 * own bit offset — bits 49-55 fall in byte 6, which only self-consistently
 * places the struct at 7 bytes total; a separately-reported "9 bytes per
 * point" summary from the same fetch contradicts that bit-numbering and
 * was discarded as unreliable — flagged in the phase report as a detail
 * that could not be independently cross-checked against a second source):
 *   byte 0: bits[5:0] x_h, bit 6 reserved, bit 7 valid (packing order
 *           inferred from GCC's LSB-first bitfield allocation for the
 *           first-declared member, matching the "reserved_6"/"valid"
 *           field names, which name their own bit positions)
 *   byte 1: x_l
 *   byte 2: y_h
 *   byte 3: y_l
 *   byte 4: area
 *   byte 5: intensity
 *   byte 6: reserved
 * x = (x_h << 8) | x_l  (14-bit), y = (y_h << 8) | y_l (16-bit). Only
 * records with the `valid` bit set are reported to the caller.
 *
 * probe() does zero I/O (DESIGN_INVARIANTS.md): the real I2C identification
 * already happened once in board preinit() via tab5_panel_detect() (see
 * boards/tab5/panel_detect.c); this driver's probe() only rejects unless
 * tab5_panel_detected() already cached the matching enum value for the
 * hint being probed.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_* from the SIC HAL plus the Tab5-private panel_detect cache
 * (also pure C99), no Arduino/ESP-IDF headers here.
 */
#include <stdint.h>
#include <string.h>
#include "sic/sic_registry.h"
#include "sic/input/touch.h"
#include "sic/bus/i2c_bus.h"
#include "boards/tab5/panel_detect.h"

#define ST712X_REG_MAX_TOUCHES  0x0009u
#define ST712X_REG_ADV_INFO     0x0010u
#define ST712X_REG_REPORT0      0x0014u
#define ST712X_REPORT_STRIDE    7u
#define ST712X_MAX_TOUCHES_CAP  10  /* CONFIG_ESP_LCD_TOUCH_MAX_POINTS-equivalent cap on the mined driver */

#define ST712X_ADV_WITH_COORD   0x08u /* bit 3 */

typedef struct {
    sic_touch_cfg_t   cfg;
    tab5_panel_kind_t required_panel;
    sic_touch_point_t cached[ST712X_MAX_TOUCHES_CAP];
    int cached_count;
} st712x_ctx_t;

/* Two independent singletons: ST7121 and ST7123 could in principle both be
 * probed against the same board IC list (only one will ever match a real
 * Tab5's cached panel kind, but each hint owns its own context/instance so
 * probing one never clobbers the other's state before the registry rejects
 * the mismatched one). */
static st712x_ctx_t g_ctx_7121, g_ctx_7123;
static touch_t       g_touch_7121, g_touch_7123;

static int st712x_read8(const st712x_ctx_t* c, uint16_t reg, uint8_t* out) {
    uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu) };
    return sic_i2c_writeread(c->cfg.i2c_bus, c->cfg.i2c_addr, a, 2, out, 1) == 1 ? 0 : -1;
}

static int st712x_read_burst(const st712x_ctx_t* c, uint16_t reg, uint8_t* buf, int len) {
    uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu) };
    return sic_i2c_writeread(c->cfg.i2c_bus, c->cfg.i2c_addr, a, 2, buf, len) == len ? 0 : -1;
}

static int st712x_read_points(const void* self, sic_touch_point_t* out, int max_points) {
    const touch_t* t = (const touch_t*)self;
    st712x_ctx_t* c = t ? (st712x_ctx_t*)t->impl : NULL;
    if (!c || !out || max_points <= 0) return -1;

    uint8_t adv_info = 0;
    if (st712x_read8(c, ST712X_REG_ADV_INFO, &adv_info) != 0) return -1;
    if (!(adv_info & ST712X_ADV_WITH_COORD)) {
        int n = c->cached_count < max_points ? c->cached_count : max_points;
        memcpy(out, c->cached, (size_t)n * sizeof *out);
        return n; /* no new report is not a release report */
    }

    uint8_t max_touches = 0;
    if (st712x_read8(c, ST712X_REG_MAX_TOUCHES, &max_touches) != 0) return -1;

    /* The ST712x report is acknowledged by reading through the last
     * register of the last hardware-supported coordinate slot -- the I2C
     * burst length must track the controller's own MAX_TOUCHES, not the
     * caller's output-buffer capacity. Truncating the burst to max_points
     * (e.g. Quake passing 5 while Doom/Wolf pass 10) leaves the
     * controller's "with coord" status asserted, so it keeps re-reporting
     * the same stale point forever. Read/ack every hardware slot, then
     * copy at most max_points valid points to the caller. */
    int hw_slots = (int)max_touches;
    if (hw_slots > ST712X_MAX_TOUCHES_CAP) hw_slots = ST712X_MAX_TOUCHES_CAP;
    if (hw_slots <= 0) { c->cached_count = 0; return 0; }

    uint8_t raw[ST712X_MAX_TOUCHES_CAP * ST712X_REPORT_STRIDE];
    if (st712x_read_burst(c, ST712X_REG_REPORT0, raw, hw_slots * (int)ST712X_REPORT_STRIDE) != 0)
        return -1;

    int j = 0;
    for (int i = 0; i < hw_slots; i++) {
        const uint8_t* p = &raw[i * (int)ST712X_REPORT_STRIDE];
        uint8_t valid = (uint8_t)((p[0] >> 7) & 0x01u);
        if (!valid) continue;

        uint16_t x = (uint16_t)(((uint16_t)(p[0] & 0x3Fu) << 8) | p[1]);
        uint16_t y = (uint16_t)(((uint16_t)p[2] << 8) | p[3]);

        c->cached[j].id       = (uint8_t)i;
        c->cached[j].x         = x;
        c->cached[j].y         = y;
        c->cached[j].pressed   = 1;
        j++;
    }
    c->cached_count = j;
    int count = j < max_points ? j : max_points;
    memcpy(out, c->cached, (size_t)count * sizeof *out);
    return count;
}

static const struct touch_vtbl_s ST712X_VT = { st712x_read_points };

static int probe_touch_st7121(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "touch_st7121") != 0 || !d->cfg) return -1;
    if (tab5_panel_detected() != TAB5_PANEL_ST7121) return -1;
    memset(&g_ctx_7121, 0, sizeof(g_ctx_7121));
    g_ctx_7121.cfg = *(const sic_touch_cfg_t*)d->cfg;
    g_ctx_7121.required_panel = TAB5_PANEL_ST7121;
    g_touch_7121.v    = &ST712X_VT;
    g_touch_7121.impl = &g_ctx_7121;
    *out = &g_touch_7121;
    return 0;
}

static int probe_touch_st7123(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "touch_st7123") != 0 || !d->cfg) return -1;
    if (tab5_panel_detected() != TAB5_PANEL_ST7123) return -1;
    memset(&g_ctx_7123, 0, sizeof(g_ctx_7123));
    g_ctx_7123.cfg = *(const sic_touch_cfg_t*)d->cfg;
    g_ctx_7123.required_panel = TAB5_PANEL_ST7123;
    g_touch_7123.v    = &ST712X_VT;
    g_touch_7123.impl = &g_ctx_7123;
    *out = &g_touch_7123;
    return 0;
}

static const sic_driver_t DRV_TOUCH_ST7121 = { "touch_st7121", SIC_F_TOUCH, probe_touch_st7121, NULL };
static const sic_driver_t DRV_TOUCH_ST7123 = { "touch_st7123", SIC_F_TOUCH, probe_touch_st7123, NULL };

void sic_register_driver_touch_st7121(void) {
    sic_registry_register(&DRV_TOUCH_ST7121);
}

void sic_register_driver_touch_st7123(void) {
    sic_registry_register(&DRV_TOUCH_ST7123);
}
