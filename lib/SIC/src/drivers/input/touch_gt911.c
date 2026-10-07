/*
 * touch_gt911.c — Goodix GT911 capacitive touch controller (I2C).
 *
 * GT911 uses 16-bit big-endian register addresses (a 2-byte address prefix
 * on every read/write), unlike most 8-bit-address chips this codebase talks
 * to elsewhere. Register map used here (well-documented, cross-checked
 * against multiple independent open-source GT911 drivers):
 *
 *   0x814E  Status register. Bit 7 = "buffer ready" (1 = new touch frame
 *           available), bits [3:0] = number of touch points reported this
 *           frame (0-5). Must be written back to 0x00 after the frame is
 *           consumed so the controller starts filling the next one.
 *   0x8150  First touch point record, 8 bytes, point N at
 *           0x8150 + N*8: [track_id, x_lsb, x_msb, y_lsb, y_msb,
 *           size_lsb, size_msb, reserved].
 *
 * x/y are masked to 12 bits on read: some GT911 configurations pack extra
 * config/resolution bits into the upper nibble of the coordinate fields, so
 * masking is the safe default used by most third-party drivers.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_* from the SIC HAL, no Arduino/ESP-IDF headers here. probe() does
 * no I/O — only parses/stores the config struct — per DESIGN_INVARIANTS.md.
 */
#include <stdint.h>
#include <string.h>
#include "sic/sic_registry.h"
#include "sic/input/touch.h"
#include "sic/bus/i2c_bus.h"

#define GT911_REG_STATUS      0x814Eu
#define GT911_REG_POINT0      0x8150u
#define GT911_POINT_STRIDE    8u
#define GT911_MAX_POINTS      5
#define GT911_STATUS_READY    0x80u
#define GT911_STATUS_NPTS_MSK 0x0Fu
#define GT911_COORD_MASK      0x0FFFu

typedef struct {
    sic_touch_cfg_t cfg;
    sic_touch_point_t cached[GT911_MAX_POINTS];
    int cached_count;
} gt911_ctx_t;

static gt911_ctx_t g_ctx;
static touch_t      g_touch;

static int gt911_read(const gt911_ctx_t* c, uint16_t reg, uint8_t* buf, int len) {
    uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu) };
    return sic_i2c_writeread(c->cfg.i2c_bus, c->cfg.i2c_addr, a, 2, buf, len) == len ? 0 : -1;
}

static int gt911_write8(const gt911_ctx_t* c, uint16_t reg, uint8_t val) {
    uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu), val };
    return sic_i2c_write(c->cfg.i2c_bus, c->cfg.i2c_addr, buf, 3) == 3 ? 0 : -1;
}

static int gt911_read_points(const void* self, sic_touch_point_t* out, int max_points) {
    const touch_t* t = (const touch_t*)self;
    gt911_ctx_t* c = t ? (gt911_ctx_t*)t->impl : NULL;
    if (!c || !out || max_points <= 0) return -1;

    uint8_t status = 0;
    if (gt911_read(c, GT911_REG_STATUS, &status, 1) != 0) return -1;

    if (!(status & GT911_STATUS_READY)) {
        int n = c->cached_count < max_points ? c->cached_count : max_points;
        memcpy(out, c->cached, (size_t)n * sizeof *out);
        return n;
    }

    int n = (int)(status & GT911_STATUS_NPTS_MSK);
    if (n > GT911_MAX_POINTS) n = GT911_MAX_POINTS;
    sic_touch_point_t parsed[GT911_MAX_POINTS];

    int got = 0;
    for (int i = 0; i < n; i++) {
        uint8_t p[8];
        if (gt911_read(c, (uint16_t)(GT911_REG_POINT0 + (unsigned)i * GT911_POINT_STRIDE), p, 8) != 0)
            return -1; /* a partial report must not look like released fingers */

        uint16_t x = (uint16_t)(p[1] | ((uint16_t)p[2] << 8));
        uint16_t y = (uint16_t)(p[3] | ((uint16_t)p[4] << 8));

        parsed[i].id       = p[0];
        parsed[i].x         = (uint16_t)(x & GT911_COORD_MASK);
        parsed[i].y         = (uint16_t)(y & GT911_COORD_MASK);
        parsed[i].pressed   = 1;
        got++;
    }

    if (gt911_write8(c, GT911_REG_STATUS, 0x00u) != 0) return -1;
    memcpy(c->cached, parsed, (size_t)got * sizeof *parsed);
    c->cached_count = got;
    int count = got < max_points ? got : max_points;
    memcpy(out, c->cached, (size_t)count * sizeof *out);
    return count;
}

static const struct touch_vtbl_s GT911_VT = { gt911_read_points };

static int probe_touch_gt911(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "touch_gt911") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_touch_cfg_t*)d->cfg;
    g_touch.v    = &GT911_VT;
    g_touch.impl = &g_ctx;
    *out = &g_touch;
    return 0;
}

static const sic_driver_t DRV_TOUCH_GT911 = { "touch_gt911", SIC_F_TOUCH, probe_touch_gt911, NULL };

void sic_register_driver_touch_gt911(void) {
    sic_registry_register(&DRV_TOUCH_GT911);
}
