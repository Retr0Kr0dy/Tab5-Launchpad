/*
 * ioexpander.c — Tab5-private shared owner of the two PI4IOE5V6408 GPIO
 * expanders (0x43 / 0x44) on the internal I2C bus.
 *
 * Between them these two chips gate almost every other Tab5 peripheral's
 * reset/enable/detect line: camera reset, touch/LCD reset, speaker amp
 * enable, external 5V rails, USB 5V rail, charge enable/quick-charge,
 * Wi-Fi power, antenna select, power-off pulse, headphone/charge-status
 * detection. This module is the single shared owner of I2C access to both
 * chips so that unrelated drivers (audio, camera, charger, Wi-Fi power
 * sequencing) never do their own blind read-modify-write on the same shared
 * output byte.
 *
 * Register byte sequences below are based on M5Stack's ESP-IDF firmware for
 * this board (M5Tab5-UserDemo), cross-checked against PI4IOE5V6408 register
 * semantics. Orion deliberately differs in one output bit: EXP1 bit 7
 * (IP2326 CHG_EN) is driven high at boot, so EXP1 OUT_SET is 0x89 rather than
 * a reference sequence that leaves charging disabled at 0x09. The remaining
 * direction/pull/interrupt programming follows the board reference.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_*, sic_delay_ms from the SIC HAL, no Arduino/ESP-IDF headers here.
 */
#include <string.h>

#include "sic/sic.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/delay.h"
#include "boards/tab5/ioexpander.h"

/* PI4IOE5V6408 register map — identical layout on both chips, different bit
 * meanings per chip (documented per-chip in ioexpander.h / below). */
#define REG_CHIP_RESET  0x01u
#define REG_IO_DIR      0x03u  /* bit=0 input, bit=1 output */
#define REG_OUT_SET     0x05u  /* output level register */
#define REG_OUT_H_IM    0x07u  /* bit=1 -> that pin forced high-impedance regardless of IO_DIR */
#define REG_IN_DEF_STA  0x09u  /* expected default state, used for interrupt compare */
#define REG_PULL_EN     0x0Bu  /* bit=1 pull resistor enabled */
#define REG_PULL_SEL    0x0Du  /* bit=0 pull-down, bit=1 pull-up */
#define REG_IN_STA      0x0Fu  /* live input status */
#define REG_INT_MASK    0x11u  /* bit=1 interrupt disabled for that pin */
#define REG_IRQ_STA     0x13u

/* Boot-time OUT_SET values, also used to seed the write shadow so
 * tab5_ioexp_set() never needs an extra I2C read before its write. */
#define EXP0_BOOT_OUT_SET 0x76u /* 0b01110110: SPK_EN, EXT5V_EN, LCD_RST, TP_RST, CAM_RST high; resets deasserted */
#define EXP1_BOOT_OUT_SET 0x89u /* 0b10001001: CHG_EN, USB5V_EN, WLAN_PWR_EN high */

static uint8_t g_out_shadow[2] = { EXP0_BOOT_OUT_SET, EXP1_BOOT_OUT_SET };
static uint8_t g_addr[2]       = { TAB5_IOEXP0_ADDR, TAB5_IOEXP1_ADDR };

/* Write a single register byte: [reg, value]. */
static int ioexp_wr(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return (sic_i2c_write(TAB5_I2C_BUS, addr, buf, 2) == 2) ? SIC_OK : SIC_EIO;
}

/* Write register, then read it back into *out (register-address write
 * followed by a repeated-start read) — used for the chip-reset readback. */
static int ioexp_wr_rd(uint8_t addr, uint8_t reg, uint8_t val, uint8_t* out) {
    uint8_t buf[2] = { reg, val };
    if (sic_i2c_write(TAB5_I2C_BUS, addr, buf, 2) != 2) return SIC_EIO;
    uint8_t reg_only = reg;
    if (sic_i2c_writeread(TAB5_I2C_BUS, addr, &reg_only, 1, out, 1) != 1) return SIC_EIO;
    return SIC_OK;
}

/* Program one PI4IOE5V6408 chip with its full boot sequence. `out_set` is
 * the chip's boot OUT_SET value (also written into g_out_shadow[idx]
 * beforehand by the caller). */
static void ioexp_program_chip(uint8_t addr,
                                uint8_t io_dir, uint8_t out_h_im,
                                uint8_t pull_sel, uint8_t pull_en,
                                uint8_t in_def_sta, int have_in_def_sta,
                                uint8_t int_mask, int have_int_mask,
                                uint8_t out_set) {
    uint8_t readback = 0;

    /* 1. Soft-reset + readback (datasheet chip-reset idiom). */
    ioexp_wr_rd(addr, REG_CHIP_RESET, 0xFFu, &readback);

    /* 2..N: direction / high-Z / pulls / interrupt-compare / interrupt-mask,
     * in the exact order specified for this board (matches reference
     * firmware's own init sequence for this exact chip pairing). */
    ioexp_wr(addr, REG_IO_DIR, io_dir);
    ioexp_wr(addr, REG_OUT_H_IM, out_h_im);
    ioexp_wr(addr, REG_PULL_SEL, pull_sel);
    ioexp_wr(addr, REG_PULL_EN, pull_en);
    if (have_in_def_sta) ioexp_wr(addr, REG_IN_DEF_STA, in_def_sta);
    if (have_int_mask)   ioexp_wr(addr, REG_INT_MASK, int_mask);

    /* Output levels last, after direction/pulls are settled. */
    ioexp_wr(addr, REG_OUT_SET, out_set);
}

void tab5_ioexp_init(void) {
    /* Nothing else has opened I2C bus 0 at the point board preinit() calls
     * us (sic_i2c_begin_bus is idempotent — see i2c_bus_espidf.c /
     * board_tpager.c's analogous XL9555 bring-up), so we own bringing the
     * bus up ourselves before any register access. */
    sic_i2c_begin_bus(TAB5_I2C_BUS, TAB5_I2C_SDA, TAB5_I2C_SCL, TAB5_I2C_HZ);

    /* Re-seed shadow bytes in case tab5_ioexp_init() is ever called more
     * than once (each call re-drives the full boot sequence). */
    g_out_shadow[0] = EXP0_BOOT_OUT_SET;
    g_out_shadow[1] = EXP1_BOOT_OUT_SET;

    /* Expander 0 @ 0x43: HP_DET(in,b7), CAM_RST(out,b6), TP_RST(out,b5),
     * LCD_RST(out,b4), NC(b3), EXT5V_EN(out,b2), SPK_EN(out,b1), RF ANT(out,b0). */
    ioexp_program_chip(TAB5_IOEXP0_ADDR,
                        0x7Fu,              /* IO_DIR:    0b01111111 */
                        0x00u,              /* OUT_H_IM:  0b00000000 */
                        0x7Fu,              /* PULL_SEL:  0b01111111 */
                        0x7Fu,              /* PULL_EN:   0b01111111 */
                        0, 0,               /* IN_DEF_STA: not programmed on this chip */
                        0, 0,               /* INT_MASK:   not programmed on this chip */
                        EXP0_BOOT_OUT_SET); /* OUT_SET:   0b01110110 */

    /* Expander 1 @ 0x44: CHG_EN(out,b7), CHG_STAT(in,b6), nCHG_QC_EN(out,b5),
     * PWROFF_PLUSE(out,b4), USB5V_EN(out,b3), NC(b2-1), WLAN_PWR_EN(out,b0). */
    ioexp_program_chip(TAB5_IOEXP1_ADDR,
                        0xB9u,              /* IO_DIR:    0b10111001 */
                        0x06u,              /* OUT_H_IM:  0b00000110 */
                        0xB9u,              /* PULL_SEL:  0b10111001 */
                        0xF9u,              /* PULL_EN:   0b11111001 */
                        0x40u, 1,           /* IN_DEF_STA: 0b01000000 */
                        0xBFu, 1,           /* INT_MASK:   0b10111111 */
                        EXP1_BOOT_OUT_SET); /* OUT_SET:   0b10001001 (CHG_EN intentionally high) */
}

int tab5_ioexp_set(int exp, int bit, int level) {
    if (exp < 0 || exp > 1) return SIC_EINVAL;
    if (bit < 0 || bit > 7) return SIC_EINVAL;

    uint8_t val = g_out_shadow[exp];
    if (level) val |= (uint8_t)(1u << bit);
    else       val &= (uint8_t)~(1u << bit);

    int rc = ioexp_wr(g_addr[exp], REG_OUT_SET, val);
    if (rc != SIC_OK) return rc;

    g_out_shadow[exp] = val;
    return SIC_OK;
}

void tab5_panel_reset_pulse(void) {
    uint8_t val = g_out_shadow[0];

    val &= (uint8_t)~((1u << 4) | (1u << 5));   /* LCD_RST, TP_RST -> low (asserted) */
    ioexp_wr(g_addr[0], REG_OUT_SET, val);
    g_out_shadow[0] = val;
    sic_delay_ms(100);

    val |= (uint8_t)((1u << 4) | (1u << 5));    /* LCD_RST, TP_RST -> high (released) */
    ioexp_wr(g_addr[0], REG_OUT_SET, val);
    g_out_shadow[0] = val;
    sic_delay_ms(100);
}

int tab5_ioexp_get_in(int exp, int bit) {
    if (exp < 0 || exp > 1) return SIC_EINVAL;
    if (bit < 0 || bit > 7) return SIC_EINVAL;

    uint8_t reg = REG_IN_STA;
    uint8_t val = 0;
    if (sic_i2c_writeread(TAB5_I2C_BUS, g_addr[exp], &reg, 1, &val, 1) != 1)
        return SIC_EIO;

    return (val & (uint8_t)(1u << bit)) ? 1 : 0;
}
