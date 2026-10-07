/*
 * panel_detect.c — Tab5-private one-time display/touch panel identification.
 *
 * Detection sequence (authoritative source: M5Tab5-UserDemo, M5Stack's
 * official ESP-IDF reference firmware for this exact board), run on the
 * internal I2C bus (bus 0):
 *
 *   1. Probe address 0x14 (GT911 touch controller's "backup"/alternate
 *      address). ACK -> TAB5_PANEL_ILI9881C_GT911. Done.
 *   2. Else probe address 0x55. ACK -> this is a combined display+touch
 *      chip (ST7121 or ST7123); read its 2-byte touch register 0x0000
 *      (1-byte value) to tell them apart:
 *        value == 1 -> TAB5_PANEL_ST7121
 *        value == 3 -> TAB5_PANEL_ST7123
 *        anything else, or the read fails -> TAB5_PANEL_ST7123 (matches
 *        the reference firmware's own fallback for this edge case).
 *   3. Neither 0x14 nor 0x55 ACKs -> TAB5_PANEL_ILI9881C_GT911 (matches the
 *      reference firmware's final fallback; a genuine detection failure
 *      still needs *a* default rather than aborting bring-up).
 *
 * REQUIRES bus 0 already open with LCD_RST/TP_RST already released — i.e.
 * tab5_ioexp_init() must run first. Board preinit() owns that ordering;
 * this file only documents and relies on it, it does not enforce it.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_* from the SIC HAL, no Arduino/ESP-IDF headers here.
 */
#include <stdint.h>

#include "sic/sic.h"
#include "sic/bus/i2c_bus.h"
#include "boards/tab5/ioexpander.h"  /* TAB5_I2C_BUS */
#include "boards/tab5/panel_detect.h"

#define TAB5_TOUCH_GT911_ALT_ADDR   0x14u
#define TAB5_TOUCH_COMBO_ADDR       0x55u
#define TAB5_TOUCH_COMBO_FWVER_REG0 0x00u
#define TAB5_TOUCH_COMBO_FWVER_REG1 0x00u

/* sic_i2c_scan_bus is the only backend-agnostic ACK/NACK primitive SIC
 * exposes (see i2c_bus_espidf.c / i2c_bus_arduino.cpp) — a zero-length
 * sic_i2c_write is not a reliable stand-in: the ESP-IDF backend short-
 * circuits n==0 writes to "success" without touching the bus at all. So a
 * full-bus scan is used and the address of interest is looked up in the
 * result, rather than inventing a new single-address probe primitive. */
#define TAB5_SCAN_MAX 32

static int addr_present(int bus, uint8_t addr) {
    uint8_t addrs[TAB5_SCAN_MAX];
    int n = sic_i2c_scan_bus(bus, addrs, TAB5_SCAN_MAX);
    if (n < 0) return 0;
    for (int i = 0; i < n; i++) {
        if (addrs[i] == addr) return 1;
    }
    return 0;
}

static tab5_panel_kind_t g_panel = TAB5_PANEL_UNKNOWN;

void tab5_panel_detect(void) {
    if (g_panel != TAB5_PANEL_UNKNOWN) return; /* idempotent */

    if (addr_present(TAB5_I2C_BUS, TAB5_TOUCH_GT911_ALT_ADDR)) {
        g_panel = TAB5_PANEL_ILI9881C_GT911;
        return;
    }

    if (addr_present(TAB5_I2C_BUS, TAB5_TOUCH_COMBO_ADDR)) {
        uint8_t reg[2] = { TAB5_TOUCH_COMBO_FWVER_REG0, TAB5_TOUCH_COMBO_FWVER_REG1 };
        uint8_t val = 0;
        int got = sic_i2c_writeread(TAB5_I2C_BUS, TAB5_TOUCH_COMBO_ADDR, reg, 2, &val, 1);

        if (got == 1 && val == 1u) {
            g_panel = TAB5_PANEL_ST7121;
        } else if (got == 1 && val == 3u) {
            g_panel = TAB5_PANEL_ST7123;
        } else {
            /* Neither value, or the read failed outright: fall back to
             * ST7123, matching the reference firmware's own behavior. */
            g_panel = TAB5_PANEL_ST7123;
        }
        return;
    }

    /* Neither address ACKed at all: final fallback. */
    g_panel = TAB5_PANEL_ILI9881C_GT911;
}

tab5_panel_kind_t tab5_panel_detected(void) {
    return g_panel;
}
