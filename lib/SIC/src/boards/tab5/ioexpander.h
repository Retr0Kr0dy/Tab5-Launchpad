/*
 * ioexpander.h — Tab5-private shared owner of the two PI4IOE5V6408 GPIO
 * expanders on the internal I2C bus (0x43 / 0x44).
 *
 * Board-private plumbing, not public SIC API: lives under src/boards/tab5/
 * and is reached via a local include path ("boards/tab5/ioexpander.h") from
 * other files under src/, never via the public sic/ include namespace. The
 * expander pair gates too many unrelated peripherals' reset/enable lines
 * for a registry-visible SIC_F_GPIO_EXPANDER capability to help here; this
 * is deliberately Tab5-only, promotable to a generic driver only if a
 * second board reuses the exact same chip.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_I2C_BUS      0
#define TAB5_I2C_SDA      31
#define TAB5_I2C_SCL      32
#define TAB5_I2C_HZ       400000u

/* Grove/Port-A external I2C bus — genuinely independent from the internal
 * bus above. Opened for the Tab5 keyboard accessory; see main.c's boot
 * sequence for the sic_i2c_begin_bus() call. */
#define TAB5_GROVE_I2C_BUS  1
#define TAB5_GROVE_I2C_SDA  53
#define TAB5_GROVE_I2C_SCL  54
#define TAB5_GROVE_I2C_HZ   400000u

/* Real Tab5 keyboard accessory's 2x4 GPIO header -- a physically separate
 * connector from the 4-pin Grove/Port-A jack above. The accessory answers
 * I2C on neither the internal bus (0) nor Grove (1); pins confirmed both
 * from M5Stack's official protocol doc (Tab5_Keyboard-I2C-Protocol-EN-V1.0.pdf)
 * and M5Tab5-Keyboard-UserDemo's own driver header
 * (M5_TAB5_KB_DEFAULT_SDA/SCL/INT = 0/1/50). The accessory is a custom
 * STM32F030C8T6 controller at I2C address 0x6D speaking its own protocol,
 * not a TCA8418. See main/kbd_accessory.c for the driver -- implemented in
 * Orion's app layer, not SIC (see board_tab5.c's keyboard-accessory comment
 * for why). */
#define TAB5_KBD_I2C_BUS  2
#define TAB5_KBD_I2C_SDA  0
#define TAB5_KBD_I2C_SCL  1
#define TAB5_KBD_I2C_HZ   400000u

#define TAB5_IOEXP0_ADDR  0x43u  /* HP_DET, CAM_RST, TP_RST, LCD_RST, EXT5V_EN, SPK_EN, RF antenna select */
#define TAB5_IOEXP1_ADDR  0x44u  /* CHG_EN, CHG_STAT, nCHG_QC_EN, PWROFF_PLUSE, USB5V_EN, WLAN_PWR_EN */

/*
 * Programs both PI4IOE5V6408 expanders with the boot-time register sequences
 * documented in ioexpander.c (direction, pull, high-impedance, and initial
 * output levels). Opens I2C bus 0 itself (sic_i2c_begin_bus) — nothing else
 * has done so at the point board preinit() runs. Call exactly once, as the
 * first thing board preinit() does; safe to call more than once (idempotent
 * via sic_i2c_begin_bus's own idempotency), but re-running the register
 * sequence will re-drive every output to its boot-time level, so callers
 * that have since changed a bit with tab5_ioexp_set() should not re-init.
 */
void tab5_ioexp_init(void);

/*
 * Read-modify-write a single output bit on expander `exp` (0 or 1, selecting
 * TAB5_IOEXP0_ADDR / TAB5_IOEXP1_ADDR) via the in-process OUT_SET shadow byte
 * — a single I2C write, no read-before-write. `bit` is 0-7, `level` is 0/1.
 * Returns 0 on success, negative sic_err_t (e.g. SIC_EINVAL, SIC_EIO) on error.
 */
int tab5_ioexp_set(int exp, int bit, int level);

/*
 * Live read of a single input bit from expander `exp`'s IN_STA register
 * (HP_DET, CHG_STAT, and any other input-wired bit change at runtime, so
 * this is never shadowed/cached). Returns 0 or 1 on success, negative
 * sic_err_t on error (bad exp/bit, or I2C failure).
 */
int tab5_ioexp_get_in(int exp, int bit);

/*
 * Hardware reset pulse for the panel/touch reset lines (expander 0, bits 4
 * and 5 = LCD_RST/TP_RST): drive both low, hold 100ms, drive both back high,
 * hold another 100ms. tab5_ioexp_init() only ever sets these bits high once
 * as part of its boot-time register program — that is NOT a reset pulse,
 * just the chip's power-on default. The ILI9881C/ST7121/ST7123 panel ICs
 * need an actual low->high transition to latch into a known state; skipping
 * it left the panel driver hung busy-polling a PHY/ready bit that the panel
 * never asserts (reproduced with the real M5Stack BSP's own
 * esp_lcd_ili9881c component, not just SGFX's). Matches M5Tab5-UserDemo's
 * bsp_reset_tp() (m5stack_tab5.c) exactly. Call after tab5_ioexp_init(),
 * before tab5_panel_detect() and before SGFX's DSI panel init.
 */
void tab5_panel_reset_pulse(void);

#ifdef __cplusplus
}
#endif
