/*
 * board_tab5.c — M5Stack Tab5 board descriptor.
 *
 * Wires together: the two shared GPIO expanders + one-time panel/touch
 * identification (board-private, not registry drivers), and the
 * SIC_F_TOUCH/RTC/IMU/CHARGER/SD/CAMERA/MIC/AMP registry drivers. Follows
 * board_tpager.c's exact shape: static per-IC cfg structs, a static
 * sic_board_ic_t[] array, one sic_board_t, one preinit().
 *
 * ── Touch: 3 IC entries, not a runtime-computed array ──────────────────────
 * Tab5 ships with one of three touch chips (GT911, ST7121, ST7123 — see
 * boards/tab5/panel_detect.c) and which one is fitted can only be learned at
 * runtime. sic_board_t.ics is `const sic_board_ic_t*` / compile-time static
 * data (include/sic/sic_registry.h) — there is no hook for board preinit()
 * to hand back a different array depending on what tab5_panel_detect() just
 * found. So all 3 touch hints are declared below unconditionally, and each
 * driver's own probe() is trusted to self-select:
 *   - touch_st7121 / touch_st7123 (src/drivers/input/touch_st712x.c) check
 *     tab5_panel_detected() themselves and return -1 (no match) unless the
 *     cached panel kind is exactly theirs.
 *   - touch_gt911 (src/drivers/input/touch_gt911.c) does NOT check
 *     tab5_panel_detected() at all. It is a generic, non-Tab5-aware driver
 *     (other future boards may reuse
 *     it), and DESIGN_INVARIANTS.md's "probe() must always succeed if the
 *     config struct is valid" is a real constraint it honours literally:
 *     unlike the ST7121/ST7123 combo chips (which share Tab5's touch I2C
 *     address 0x55 with the *display* identity register and therefore need
 *     an explicit gate to avoid two drivers both claiming that one address),
 *     GT911's address (0x14) doesn't collide with anything else on this
 *     board, so it is safe to let a genuinely absent chip simply NACK at
 *     first real I2C use instead of gating in probe().
 *
 *   Net effect on real ST7121/ST7123 hardware: touch_gt911's IC entry still
 *   registers a (spurious, harmless) SIC_F_TOUCH instance alongside the
 *   correct combo-chip one — its read_points() will just fail every I2C
 *   transaction against an address nothing answers on that hardware, same
 *   as any other genuinely-absent-device case DESIGN_INVARIANTS.md already
 *   expects callers to tolerate. To keep sic_touch(0) always resolving to
 *   the *real* device despite that, g_ics below lists touch_gt911 FIRST and
 *   the two combo hints after: registry.c's instance list is LIFO ("last
 *   registered = index 0"), so whichever combo driver actually matches the
 *   cached panel is added after GT911 and becomes index 0. On genuine
 *   ILI9881C+GT911 hardware, both combo probes reject and GT911 is the only
 *   (and therefore also index-0) entry.
 *
 * ── preinit() ordering ──────────────────────────────────────────────────
 * 1. tab5_ioexp_init()   — must run first: every other step below needs a
 *    shared reset/rail line this call releases (LCD_RST/TP_RST for panel
 *    detection, CAM_RST is driven later by the camera driver itself but the
 *    expander I2C access it uses only works after this, WLAN_PWR_EN's boot
 *    level is also set here). It also opens I2C bus 0, which nothing else
 *    in preinit() can assume is already open otherwise.
 * 2. tab5_panel_reset_pulse() — performs the actual LCD_RST/TP_RST
 *    low-to-high pulse after the expander is configured.
 * 3. tab5_panel_detect() — needs bus 0 open and the panel/touch reset pulse
 *    complete; its cached result lets the three touch-driver probe()s above
 *    self-select correctly once sic_begin_legacy()'s IC loop runs.
 * 4. tab5_wifi_power_init() — independent of panel detection, but depends on
 *    tab5_ioexp_set() (step 1 must have seeded the expander shadow) and is
 *    placed last so the ESP32-C6 gets the longest possible boot-settle time
 *    before app code might touch Wi-Fi/BLE.
 */
#include "sic/sic_board.h"
#include "sic/input/touch.h"
#include "sic/power/rtc.h"
#include "sic/motion/imu.h"
#include "sic/storage/sd_sdmmc.h"
#include "sic/video/camera.h"
#include "sic/audio/codec_es7210.h"
#include "sic/audio/codec_es8388.h"

#include "boards/tab5/ioexpander.h"
#include "boards/tab5/panel_detect.h"
#include "boards/tab5/wifi_power.h"

/* ── Touch ─────────────────────────────────────────────────────────────── */

/* GT911 alternate address, matching panel_detect.c's own probe address for
 * this same chip (TAB5_TOUCH_GT911_ALT_ADDR, boards/tab5/panel_detect.c). */
static const sic_touch_cfg_t g_touch_gt911_cfg = {
    .i2c_bus  = TAB5_I2C_BUS,
    .i2c_addr = 0x14,
    .pin_int  = 23,
    .max_x    = 720,
    .max_y    = 1280,
};

/* ST7121 / ST7123 share Tab5's combo touch address (0x55, also the display
 * identity register probed once in panel_detect.c). */
static const sic_touch_cfg_t g_touch_combo_cfg = {
    .i2c_bus  = TAB5_I2C_BUS,
    .i2c_addr = 0x55,
    .pin_int  = 23,
    .max_x    = 720,
    .max_y    = 1280,
};

/* ── Keyboard accessory ───────────────────────────────────────────────────
 * Real M5Stack Tab5 keyboard accessory. NOT a TCA8418 -- an I2C scan against
 * address 0x34 on every bus comes back completely clean (zero devices, zero
 * stuck/UU addresses). The official M5Stack protocol
 * doc (Tab5_Keyboard-I2C-Protocol-EN-V1.0.pdf) and M5Stack's own
 * M5Tab5-Keyboard-UserDemo firmware confirm the real accessory is a custom
 * STM32F030C8T6-based controller at I2C address 0x6D, on its own dedicated
 * 2x4 GPIO header (SDA=GPIO0/SCL=GPIO1/INT=GPIO50 -- TAB5_KBD_I2C_BUS, see
 * ioexpander.h), speaking a completely different register protocol (a
 * device-side debounced press/release FIFO, not a raw matrix to scan) and
 * a 5-row x 14-col (70-key) physical layout -- too many keys for SIC's
 * generic kscan_t 64-bit state bitmap. Since the real chip already delivers
 * discrete edge events rather than a full-state snapshot to diff, it
 * doesn't naturally fit SIC's poll-and-diff kscan model anyway. Implemented
 * instead as a dedicated Orion app-layer driver
 * (Tab5-Orion/main/kbd_accessory.c), the same precedent already used for
 * USB HID and Wi-Fi/BT for real features
 * that don't fit SIC/SGFX's existing capability-registry shape. */

/* ── RTC / IMU ─────────────────────────────────────────────────────────── */

static const sic_rtc_cfg_t g_rtc_cfg = {
    .i2c_bus  = TAB5_I2C_BUS,
    .i2c_addr = 0x32,
};

static const sic_imu_cfg_t g_imu_cfg = {
    .i2c_bus  = TAB5_I2C_BUS,
    .i2c_addr = 0x68,  /* BMI270 primary I2C address on Tab5 */
};

/* ── IP2326 charger status/control: expander-wired, no per-driver cfg ───── */
/* cfg is deliberately NULL for the "tab5_chgctl" IC entry below. */

/* ── SD (4-bit SDMMC) ─────────────────────────────────────────────────── */

static const sic_sd_sdmmc_cfg_t g_sd_cfg = {
    .clk_pin  = 43,
    .cmd_pin  = 44,
    .d0_pin   = 39,
    .d1_pin   = 40,
    .d2_pin   = 41,
    .d3_pin   = 42,
    .ldo_chan = 4,
    .ldo_mv   = 3300,
};

/* ── Camera (MIPI-CSI, sensor auto-probed by esp_video) ──────────────────── */

static const sic_camera_cfg_t g_cam_cfg = {
    .pin_clk = 36,
    .clk_hz  = 24000000u,
};

/* ── Audio: ES7210 (mic, 0x40) + ES8388 (amp, 0x10), shared I2S0 ─────────── */
/* Pins: MCLK=30, BCLK=27, WS=29, DOUT=26, DIN=28. */

static const sic_es7210_cfg_t g_mic_cfg = {
    .i2c_bus  = TAB5_I2C_BUS,
    .i2c_addr = 0x40,
    .pin_mclk = 30,
    .pin_bclk = 27,
    .pin_ws   = 29,
    .pin_dout = 26,
    .pin_din  = 28,
};

static const sic_es8388_cfg_t g_amp_cfg = {
    .i2c_bus  = TAB5_I2C_BUS,
    .i2c_addr = 0x10,
    .pin_mclk = 30,
    .pin_bclk = 27,
    .pin_ws   = 29,
    .pin_dout = 26,
    .pin_din  = 28,
};

/* ── preinit() ─────────────────────────────────────────────────────────── */

static void tab5_preinit(void) {
    tab5_ioexp_init();          /* 1. release every shared reset/rail line first */
    tab5_panel_reset_pulse();   /* 2. actual LCD_RST/TP_RST low->high pulse (see header) */
    tab5_panel_detect();        /* 3. cache panel/touch identity for touch probe()s */
    tab5_wifi_power_init();     /* 4. power/reset the C6 co-processor, longest settle time */
}

/* ── IC table ──────────────────────────────────────────────────────────── */

static const struct sic_board_ic_s g_ics[] = {
    /* GT911 listed first: registry.c's per-function list is LIFO, so
     * whichever ST7121/ST7123 entry actually matches the detected panel
     * (added after this one) ends up at sic_touch(0). See file header. */
    { SIC_F_TOUCH,   "touch_gt911",  &g_touch_gt911_cfg },
    { SIC_F_TOUCH,   "touch_st7121", &g_touch_combo_cfg },
    { SIC_F_TOUCH,   "touch_st7123", &g_touch_combo_cfg },

    { SIC_F_RTC,     "rx8130",       &g_rtc_cfg  },
    { SIC_F_IMU,     "bmi270",       &g_imu_cfg  },
    { SIC_F_CHARGER, "tab5_chgctl",  NULL        },
    { SIC_F_SD,      "sd_sdmmc",     &g_sd_cfg   },
    { SIC_F_CAMERA,  "tab5_cam_espidf", &g_cam_cfg },
    { SIC_F_MIC,     "codec_es7210", &g_mic_cfg  },
    { SIC_F_AMP,     "codec_es8388", &g_amp_cfg  },
};

const struct sic_board_s SIC_BOARD_TAB5 = {
    .name     = "tab5",
    .ics      = g_ics,
    .ic_count = sizeof(g_ics) / sizeof(g_ics[0]),
    .preinit  = tab5_preinit,
};
