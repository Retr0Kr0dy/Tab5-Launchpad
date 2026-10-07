/*
 * rtc_rx8130.c — Epson RX8130 RTC (I2C, addr 0x32, internal bus @ 400kHz).
 *
 * Register map and BCD conversion convention mined (register addresses and
 * the BCD<->binary convention only, logic clean-room reimplemented) from
 * M5Stack's own official ESP-IDF firmware for this exact board:
 * M5Tab5-UserDemo platforms/tab5/main/hal/utils/rx8130/{rx8130.h,rx8130.cpp}.
 *
 *   0x10 SEC, 0x11 MIN, 0x12 HOUR, 0x13 WDAY, 0x14 MDAY, 0x15 MONTH,
 *   0x16 YEAR — seven consecutive BCD registers, burst-readable/writable
 *   in one transaction starting at SEC (matches the reference driver's own
 *   7-byte burst read/write starting at RX8130_REG_SEC).
 *   0x1D FLAG register: bit 1 = VLF (voltage-low flag), set by the chip
 *   after a power loss; the reference driver clears the whole flag
 *   register (writes 0x00) as part of its own IRQ-flag-clear helper. This
 *   driver does the same, once, lazily on first get_time()/set_time()
 *   call (probe() must not do I/O per DESIGN_INVARIANTS.md).
 *   0x1E CONTROL0 register: bit 6 = STOP. The reference driver sets STOP
 *   before rewriting the clock/calendar burst and clears it again after,
 *   to avoid a rollover mid-write; this driver does the same in set_time().
 *
 * NOTE on WDAY (0x13): this driver treats it as a plain 0-6 BCD index
 * (0=Sunday), exactly matching the mined reference driver's own
 * dec2bcd()/bcd2dec() round-trip on that register. Epson's RX8xxx family
 * datasheets conventionally document the weekday register as a one-hot
 * bitmask (bit0=Sun .. bit6=Sat) rather than a binary/BCD index, which
 * would conflict with this. Since the mined source is M5Stack's own
 * shipped driver for this exact board/chip pairing, this implementation
 * matches it rather than the generic Epson convention — flagged here as
 * an assumption that should be confirmed against real hardware before
 * relying on sic_rtc_time_t::wday for anything beyond cosmetic display.
 *
 * Standard BCD conversion, same convention used elsewhere in SIC:
 *   bin = (bcd & 0x0F) + ((bcd >> 4) * 10), and the documented inverse.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_* from the SIC HAL, no Arduino/ESP-IDF headers here. probe()
 * does no I/O — only parses/stores the config struct.
 */
#include <stdint.h>
#include <string.h>
#include "sic/sic_registry.h"
#include "sic/power/rtc.h"
#include "sic/bus/i2c_bus.h"

#define RX8130_REG_SEC     0x10u
#define RX8130_REG_MIN     0x11u
#define RX8130_REG_HOUR    0x12u
#define RX8130_REG_WDAY    0x13u
#define RX8130_REG_MDAY    0x14u
#define RX8130_REG_MONTH   0x15u
#define RX8130_REG_YEAR    0x16u
#define RX8130_REG_FLAG    0x1Du
#define RX8130_REG_CTRL0   0x1Eu

#define RX8130_FLAG_VLF    0x02u /* bit 1 */
#define RX8130_CTRL_STOP   0x40u /* bit 6 */

#define RX8130_BURST_LEN   7u /* SEC..YEAR, one register per byte */

typedef struct {
    sic_rtc_cfg_t cfg;
    int           flag_checked;
} rx8130_ctx_t;

static rx8130_ctx_t g_ctx;
static rtc_t         g_rtc;

static uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v & 0x0Fu) + ((v >> 4) * 10u)); }
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

static int rx8130_read8(const rx8130_ctx_t* c, uint8_t reg, uint8_t* out) {
    return sic_i2c_writeread(c->cfg.i2c_bus, c->cfg.i2c_addr, &reg, 1, out, 1) == 1 ? 0 : -1;
}

static int rx8130_write8(const rx8130_ctx_t* c, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return sic_i2c_write(c->cfg.i2c_bus, c->cfg.i2c_addr, buf, 2) == 2 ? 0 : -1;
}

static int rx8130_read_burst(const rx8130_ctx_t* c, uint8_t reg, uint8_t* buf, int len) {
    return sic_i2c_writeread(c->cfg.i2c_bus, c->cfg.i2c_addr, &reg, 1, buf, len) == len ? 0 : -1;
}

static int rx8130_write_burst(const rx8130_ctx_t* c, uint8_t reg, const uint8_t* data, int len) {
    if (len > 15) return -1; /* generous static cap; RX8130 burst here is always 7 */
    uint8_t buf[1 + 15];
    buf[0] = reg;
    memcpy(&buf[1], data, (size_t)len);
    return sic_i2c_write(c->cfg.i2c_bus, c->cfg.i2c_addr, buf, len + 1) == (len + 1) ? 0 : -1;
}

/* Clear the voltage-low flag once after power-up, lazily (not from probe()).
 * Best-effort: a failed read/write here does not block get_time/set_time. */
static void rx8130_check_vlf_once(rx8130_ctx_t* c) {
    if (c->flag_checked) return;
    c->flag_checked = 1;

    uint8_t flag = 0;
    if (rx8130_read8(c, RX8130_REG_FLAG, &flag) == 0 && (flag & RX8130_FLAG_VLF)) {
        (void)rx8130_write8(c, RX8130_REG_FLAG, 0x00u);
    }
}

static int rtc_get_time(const void* self, sic_rtc_time_t* out) {
    const rtc_t* t = (const rtc_t*)self;
    rx8130_ctx_t* c = t ? (rx8130_ctx_t*)t->impl : NULL;
    if (!c || !out) return -1;

    rx8130_check_vlf_once(c);

    uint8_t d[RX8130_BURST_LEN];
    if (rx8130_read_burst(c, RX8130_REG_SEC, d, (int)RX8130_BURST_LEN) != 0) return -1;

    out->sec  = bcd2dec(d[0] & 0x7Fu);
    out->min  = bcd2dec(d[1] & 0x7Fu);
    out->hour = bcd2dec(d[2] & 0x3Fu); /* 24h only */
    out->wday = bcd2dec(d[3] & 0x7Fu);
    out->mday = bcd2dec(d[4] & 0x3Fu);
    out->mon  = bcd2dec(d[5] & 0x1Fu);
    out->year = (uint16_t)(2000u + bcd2dec(d[6]));
    return 0;
}

static int rtc_set_time(const void* self, const sic_rtc_time_t* in) {
    const rtc_t* t = (const rtc_t*)self;
    rx8130_ctx_t* c = t ? (rx8130_ctx_t*)t->impl : NULL;
    if (!c || !in) return -1;
    if (in->year < 2000u || in->year > 2099u) return -1;

    rx8130_check_vlf_once(c);

    uint8_t ctrl0 = 0;
    if (rx8130_read8(c, RX8130_REG_CTRL0, &ctrl0) != 0) return -1;
    if (rx8130_write8(c, RX8130_REG_CTRL0, (uint8_t)(ctrl0 | RX8130_CTRL_STOP)) != 0) return -1;

    uint8_t d[RX8130_BURST_LEN] = {
        dec2bcd(in->sec), dec2bcd(in->min), dec2bcd(in->hour),
        dec2bcd(in->wday), dec2bcd(in->mday), dec2bcd(in->mon),
        dec2bcd((uint8_t)(in->year - 2000u))
    };
    int rc = rx8130_write_burst(c, RX8130_REG_SEC, d, (int)RX8130_BURST_LEN);

    /* Always try to clear STOP again, even if the burst write failed, so a
     * partial failure does not leave the clock halted. */
    (void)rx8130_write8(c, RX8130_REG_CTRL0, (uint8_t)(ctrl0 & (uint8_t)~RX8130_CTRL_STOP));

    return rc;
}

static const struct rtc_vtbl_s RX8130_VT = { rtc_get_time, rtc_set_time };

static int probe_rtc_rx8130(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "rx8130") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_rtc_cfg_t*)d->cfg;
    g_rtc.v    = &RX8130_VT;
    g_rtc.impl = &g_ctx;
    *out = &g_rtc;
    return 0;
}

static const sic_driver_t DRV_RTC_RX8130 = { "rx8130", SIC_F_RTC, probe_rtc_rx8130, NULL };

void sic_register_driver_rtc_rx8130(void) {
    sic_registry_register(&DRV_RTC_RX8130);
}
