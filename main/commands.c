/*
 * commands.c — Tab5-Launchpad konsole diagnostic commands.
 *
 * Trimmed from Tab5-Orion's command set: ported verbatim where generic
 * (sys/mem/hw/panel/touch/rtc/imu/bat/sd/ioexp/i2c/gfx/bl/screenshot/
 * send/recv/ls), dropped where tied to a subsystem this firmware doesn't
 * have (usb host, wifi, bt, keyboard accessory, camera, mic/amp audio,
 * doom/video/elfrun). `midi` is new, a manual MIDI send test.
 *
 * konsole provides help / clear / version / reboot itself; everything here
 * is hardware (or, for midi, the one real feature).
 */

#include "orion.h"
#include "ui_theme.h"
#include "ui_components.h"
#include "ymodem.h"
#include "usb_midi_device.h"
#include "launchpad_grid.h"
#include "launchpad_modes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "sic/sic.h"
#include "sic/bus/i2c_bus.h"
#include "sic/storage/sd.h"

#include "boards/tab5/ioexpander.h"
#include "boards/tab5/panel_detect.h"

#define ORION_GROVE_I2C_BUS 1
#define ORION_GROVE_I2C_SDA 53
#define ORION_GROVE_I2C_SCL 54

static int cmd_sys(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    kon_printf(ks, "fw      : Tab5-Launchpad %s\r\n", ORION_VERSION);
    kon_printf(ks, "board   : M5Stack Tab5 (ESP32-P4)\r\n");
    kon_printf(ks, "console : %s\r\n", orion_console_name());
    kon_printf(ks, "uptime  : %u ms\r\n", (unsigned)orion_millis());
    kon_printf(ks, "canary  : %u ms since main UI task last yielded"
                    " (>2000=warn, >8000=stalled; also drives the keyboard heartbeat LED)\r\n",
               (unsigned)orion_canary_age_ms());

    sic_sysinfo_t si;
    if (sic_sysinfo(&si) == 0) {
        kon_printf(ks, "chip    : %s rev%u @ %uMHz\r\n",
                   si.chip_model ? si.chip_model : "?",
                   (unsigned)si.chip_rev, (unsigned)si.cpu_mhz);
        kon_printf(ks, "flash   : %u bytes @ %uHz\r\n",
                   (unsigned)si.flash_bytes, (unsigned)si.flash_hz);
        kon_printf(ks, "psram   : %u bytes\r\n", (unsigned)si.psram_bytes);
    }
    return 0;
}
static int cmd_mem(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    const uint32_t icaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t pcaps = MALLOC_CAP_SPIRAM  | MALLOC_CAP_8BIT;
    kon_printf(ks, "internal : free=%u largest=%u min-ever=%u\r\n",
               (unsigned)heap_caps_get_free_size(icaps),
               (unsigned)heap_caps_get_largest_free_block(icaps),
               (unsigned)heap_caps_get_minimum_free_size(icaps));
    kon_printf(ks, "psram    : free=%u largest=%u min-ever=%u\r\n",
               (unsigned)heap_caps_get_free_size(pcaps),
               (unsigned)heap_caps_get_largest_free_block(pcaps),
               (unsigned)heap_caps_get_minimum_free_size(pcaps));
    return 0;
}
static int cmd_hw(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    /* The full current sic_func_id_t enum (include/sic/sic.h), not T-Pager's
     * older subset: touch / imu / rtc / camera are Phase 4-5 additions. */
    for (int f = 0; f < (int)SIC_F__COUNT; ++f) {
        sic_func_id_t fn = (sic_func_id_t)f;
        int n = sic_count_fn(fn);
        kon_printf(ks, "%-9s: ", sic_func_name(fn));
        if (n == 0) { kon_printf(ks, "none\r\n"); continue; }
        for (int i = 0; i < n; ++i)
            kon_printf(ks, "%s%s", sic_name_fn(fn, i), (i + 1 < n) ? ", " : "");
        kon_printf(ks, "\r\n");
    }
    return 0;
}
static const char* panel_kind_name(tab5_panel_kind_t k)
{
    switch (k) {
    case TAB5_PANEL_ILI9881C_GT911: return "ILI9881C + GT911";
    case TAB5_PANEL_ST7121:         return "ST7121 (display+touch combo)";
    case TAB5_PANEL_ST7123:         return "ST7123 (display+touch combo)";
    default:                        return "UNKNOWN";
    }
}

static int cmd_panel(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    tab5_panel_kind_t k = tab5_panel_detected();
    kon_printf(ks, "detected : %s (kind=%d)\r\n", panel_kind_name(k), (int)k);
    kon_printf(ks, "sgfx drv : %s\r\n", orion_panel_name());
    if (k == TAB5_PANEL_UNKNOWN)
        kon_printf(ks, "note     : probe at I2C 0x55 found nothing; display "
                       "fell back to ILI9881C timings\r\n");
    return 0;
}

/* ── touch ─────────────────────────────────────────────────────────────── */

#define ORION_TOUCH_MAX 10

static int cmd_touch(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    const touch_t* t = sic_touch(0);
    if (!t || !t->v || !t->v->read_points) {
        kon_printf(ks, "touch: not available\r\n");
        return -1;
    }
    kon_printf(ks, "touch (%s) - press the screen, any key to exit\r\n",
               sic_name_fn(SIC_F_TOUCH, 0));
    orion_console_drain();

    sic_touch_point_t pts[ORION_TOUCH_MAX];
    int last_n = -1;
    for (;;) {
        if (orion_console_getch() >= 0) break;
        int n = t->v->read_points(t, pts, ORION_TOUCH_MAX);
        if (n < 0) {
            if (last_n != n) kon_printf(ks, "read_points rc=%d\r\n", n);
            last_n = n;
            orion_delay_ms(50);
            continue;
        }
        if (n > 0) {
            for (int i = 0; i < n; ++i)
                kon_printf(ks, "  id=%-2u x=%-4u y=%-4u pressed=%u\r\n",
                           (unsigned)pts[i].id, (unsigned)pts[i].x,
                           (unsigned)pts[i].y, (unsigned)pts[i].pressed);
        } else if (last_n > 0) {
            kon_printf(ks, "  (release)\r\n");
        }
        last_n = n;
        orion_delay_ms(20);
    }
    kon_printf(ks, "done\r\n");
    return 0;
}
static const char* kWday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

/* Sakamoto's method — so `rtc set` writes a weekday that can be checked
 * against the readback: the RX8130 driver writes WDAY as a
 * plain BCD 0-6 index (matching the reference firmware's own driver for this chip), which
 * conflicts with Epson's usual one-hot weekday bitmask convention for the
 * RX8xxx family. If `rtc` reads back a weekday that does not match the date
 * you just set, the one-hot encoding is the right one. */
static int day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int cmd_rtc(struct konsole* ks, int argc, char** argv)
{
    const rtc_t* r = sic_rtc(0);
    if (!r || !r->v) { kon_printf(ks, "rtc: not available\r\n"); return -1; }

    if (argc >= 2 && strcmp(argv[1], "set") == 0) {
        if (argc < 8) {
            kon_printf(ks, "usage: rtc set YYYY MM DD HH MM SS\r\n");
            return -1;
        }
        if (!r->v->set_time) { kon_printf(ks, "rtc: set unsupported\r\n"); return -1; }

        sic_rtc_time_t tm;
        tm.year = (uint16_t)strtol(argv[2], NULL, 10);
        tm.mon  = (uint8_t) strtol(argv[3], NULL, 10);
        tm.mday = (uint8_t) strtol(argv[4], NULL, 10);
        tm.hour = (uint8_t) strtol(argv[5], NULL, 10);
        tm.min  = (uint8_t) strtol(argv[6], NULL, 10);
        tm.sec  = (uint8_t) strtol(argv[7], NULL, 10);

        if (tm.year < 2000 || tm.year > 2099 || tm.mon < 1 || tm.mon > 12 ||
            tm.mday < 1 || tm.mday > 31 || tm.hour > 23 || tm.min > 59 ||
            tm.sec > 59) {
            kon_printf(ks, "rtc: value out of range\r\n");
            return -1;
        }
        tm.wday = (uint8_t)day_of_week(tm.year, tm.mon, tm.mday);

        int rc = r->v->set_time(r, &tm);
        kon_printf(ks, "rtc set rc=%d (wday computed as %s)\r\n",
                   rc, kWday[tm.wday]);
        if (rc != 0) return rc;
        /* fall through and read it straight back */
    } else if (argc >= 2) {
        kon_printf(ks, "usage: rtc [set YYYY MM DD HH MM SS]\r\n");
        return -1;
    }

    if (!r->v->get_time) { kon_printf(ks, "rtc: get unsupported\r\n"); return -1; }
    sic_rtc_time_t now;
    memset(&now, 0, sizeof now);
    int rc = r->v->get_time(r, &now);
    if (rc != 0) { kon_printf(ks, "rtc: read failed rc=%d\r\n", rc); return rc; }

    kon_printf(ks, "%04u-%02u-%02u %02u:%02u:%02u  wday=%u (%s)\r\n",
               (unsigned)now.year, (unsigned)now.mon, (unsigned)now.mday,
               (unsigned)now.hour, (unsigned)now.min, (unsigned)now.sec,
               (unsigned)now.wday,
               now.wday < 7 ? kWday[now.wday] : "?");
    return 0;
}
static int milli(float v) { return (int)(v * 1000.0f); }

static int cmd_imu(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    const imu_t* im = sic_imu(0);
    if (!im || !im->v || !im->v->read) {
        kon_printf(ks, "imu: not available\r\n");
        return -1;
    }
    kon_printf(ks, "imu (%s) - accel mg / gyro mdps, any key to exit\r\n",
               sic_name_fn(SIC_F_IMU, 0));
    kon_printf(ks, "note: raw registers only, no Bosch config blob uploaded - "
                   "readings may be uncalibrated\r\n");
    orion_console_drain();

    for (;;) {
        if (orion_console_getch() >= 0) break;
        sic_imu_sample_t s;
        memset(&s, 0, sizeof s);
        int rc = im->v->read(im, &s);
        if (rc != 0) {
            kon_printf(ks, "read rc=%d\r\n", rc);
            orion_delay_ms(200);
            continue;
        }
        kon_printf(ks, "\ra %+6d %+6d %+6d   g %+7d %+7d %+7d   ",
                   milli(s.ax), milli(s.ay), milli(s.az),
                   milli(s.gx), milli(s.gy), milli(s.gz));
        orion_delay_ms(100);
    }
    kon_printf(ks, "\r\ndone\r\n");
    return 0;
}
static int cmd_bat(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    sic_battery_t bat = {0.0f, -1, 0.0f};
    int r = sic_battery_read(&bat);
    if (r < 0) {
        kon_printf(ks, "battery: not available (rc=%d)\r\n", r);
    } else {
        int mv = (int)(bat.voltage_v * 1000.0f);
        int cur_mma = (int)(bat.current_ma * 1000.0f);   /* micro-mA, for 3-decimal print */
        int cur_neg = cur_mma < 0;
        if (cur_neg) cur_mma = -cur_mma;
        kon_printf(ks, "battery: %d.%03dV  %d%%  (percent curve is a "
                       "placeholder linear 6.0-8.4V map)\r\n",
                   mv / 1000, mv % 1000, bat.percent);
        kon_printf(ks, "current: %s%d.%03dmA  (+ = charging, - = discharging; "
                       "normalized by battery_ina226.c)\r\n",
                   cur_neg ? "-" : "+", cur_mma / 1000, cur_mma % 1000);
    }

    const charger_t* chg = sic_charger(0);
    if (!chg || !chg->v) { kon_printf(ks, "charger: not available\r\n"); return 0; }
    /* Tab5's IP2326 status pin only proves active charging. Map the generic
     * SIC zero state to what it actually means on this board. */
    static const char* kStates[] = {"not charging", "charging", "full", "fault"};
    int st = chg->v->get_state(chg);
    kon_printf(ks, "charger: %s\r\n",
               (st >= 0 && st < 4) ? kStates[st] : "?");
    kon_printf(ks, "note   : IP2326 CHG_STAT via expander 0x44/b6; only active "
                   "charging vs not-charging is exposed here (no reliable "
                   "fuel-gauge FULL/fault state on this signal)\r\n");
    return 0;
}
static int cmd_sd(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    const sd_t* sd = sic_sd(0);
    if (!sd || !sd->v) { kon_printf(ks, "sd: not available\r\n"); return -1; }

    if (sd->v->begin) {
        int rc = sd->v->begin(sd);
        kon_printf(ks, "mount   : rc=%d%s\r\n", rc,
                   rc == 0 ? "" : " (mount failed; driver can retry after its cooldown)");
    }
    int present = sd->v->present ? sd->v->present(sd) : -1;
    kon_printf(ks, "present : %s\r\n", present == 1 ? "yes" : "no");

    if (present == 1 && sd->v->card_size_bytes) {
        uint64_t sz = sd->v->card_size_bytes(sd);
        kon_printf(ks, "size    : %llu bytes (%llu MB)\r\n",
                   (unsigned long long)sz,
                   (unsigned long long)(sz / (1024ull * 1024ull)));
        kon_printf(ks, "mounted : /sdcard\r\n");
    }
    return 0;
}
static int cmd_ioexp(struct konsole* ks, int argc, char** argv)
{
    if (argc < 3) {
        kon_printf(ks, "usage: ioexp <exp 0|1> <bit 0-7> [0|1]\r\n");
        kon_printf(ks, "  exp0 (0x%02x): HP_DET, CAM_RST, TP_RST, LCD_RST, "
                       "EXT5V_EN, SPK_EN, antenna-select\r\n", TAB5_IOEXP0_ADDR);
        kon_printf(ks, "  exp1 (0x%02x): CHG_EN, CHG_STAT, QC_EN, "
                       "PWROFF_PLUSE, USB5V_EN, WLAN_PWR_EN\r\n", TAB5_IOEXP1_ADDR);
        kon_printf(ks, "  no level  -> read the live input register bit\r\n");
        kon_printf(ks, "  0 or 1    -> drive the output bit\r\n");
        return -1;
    }
    int exp = (int)strtol(argv[1], NULL, 0);
    int bit = (int)strtol(argv[2], NULL, 0);
    if (exp < 0 || exp > 1 || bit < 0 || bit > 7) {
        kon_printf(ks, "ioexp: exp must be 0/1 and bit 0-7\r\n");
        return -1;
    }

    if (argc >= 4) {
        int level = (int)strtol(argv[3], NULL, 0) ? 1 : 0;
        int rc = tab5_ioexp_set(exp, bit, level);
        kon_printf(ks, "ioexp set exp%d bit%d = %d -> rc=%d\r\n",
                   exp, bit, level, rc);
        /* Careful: these bits gate resets and power rails for peripherals
         * other tests depend on. Nothing re-runs tab5_ioexp_init() afterwards. */
        return rc;
    }

    int v = tab5_ioexp_get_in(exp, bit);
    if (v < 0) { kon_printf(ks, "ioexp: read failed rc=%d\r\n", v); return v; }
    kon_printf(ks, "ioexp in exp%d bit%d = %d\r\n", exp, bit, v);
    return 0;
}
static int cmd_i2c(struct konsole* ks, int argc, char** argv)
{
    int bus = (argc >= 2) ? (int)strtol(argv[1], NULL, 0) : 0;
    if (bus < 0 || bus > 1) {
        kon_printf(ks, "usage: i2c [0|1]   (0=internal, 1=Grove/Port-A)\r\n");
        return -1;
    }
    if (bus == ORION_GROVE_I2C_BUS) {
        /* Bus 0 is opened by SIC's board preinit; bus 1 is not. Idempotent. */
        int rc = sic_i2c_begin_bus(ORION_GROVE_I2C_BUS, ORION_GROVE_I2C_SDA,
                                   ORION_GROVE_I2C_SCL, 400000);
        if (rc != 0) {
            kon_printf(ks, "i2c: opening Grove bus (SDA=%d SCL=%d) failed "
                           "rc=%d\r\n",
                       ORION_GROVE_I2C_SDA, ORION_GROVE_I2C_SCL, rc);
            return rc;
        }
    }

    uint8_t addrs[32];
    int n = sic_i2c_scan_bus(bus, addrs, (int)(sizeof addrs));
    if (n < 0) { kon_printf(ks, "i2c: scan failed rc=%d\r\n", n); return n; }
    kon_printf(ks, "i2c bus %d: %d device(s)", bus, n);
    for (int i = 0; i < n; ++i) kon_printf(ks, " 0x%02x", addrs[i]);
    kon_printf(ks, "\r\n");

    /* sic_i2c_scan_bus() only reports clean ACKs; a stuck bus (SDA/SCL held,
     * usually an unpowered/miswired slave) NACKs everything and looks
     * identical to "nothing connected" unless probed per-address for the
     * timeout case separately -- matching the distinction the real
     * M5Tab5-UserDemo's own bsp_i2c_scan() makes ("UU" entries). */
    int stuck_n = 0;
    for (int a = 1; a < 0x7F; ++a) {
        if (sic_i2c_probe_status(bus, (uint8_t)a) == SIC_I2C_PROBE_STUCK) {
            if (stuck_n == 0) kon_printf(ks, "i2c bus %d: stuck/UU:", bus);
            kon_printf(ks, " 0x%02x", a);
            stuck_n++;
        }
    }
    if (stuck_n) kon_printf(ks, "\r\n");
    return 0;
}
static int cmd_gfx(struct konsole* ks, int argc, char** argv)
{
    if (!orion_display_ready()) {
        kon_printf(ks, "gfx: display not initialized\r\n");
        return -1;
    }
    if (argc >= 2 && strcmp(argv[1], "splash") == 0) {
        orion_display_splash();
        kon_printf(ks, "gfx: splash redrawn\r\n");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "info") != 0) {
        kon_printf(ks, "usage: gfx [info|splash]\r\n");
        return -1;
    }

    size_t stride = 0;
    void*  fb = orion_fb_ptr(&stride);
    kon_printf(ks, "size    : %dx%d\r\n", orion_display_w(), orion_display_h());
    kon_printf(ks, "bus     : MIPI-DSI, 2 lanes (runtime-selected panel)\r\n");
    kon_printf(ks, "panel   : %s\r\n", orion_panel_name());
    kon_printf(ks, "fb      : %p  stride=%u bytes  fmt=RGB565\r\n",
               fb, (unsigned)stride);
    kon_printf(ks, "backlight: %d%%\r\n", orion_backlight_get());
    return 0;
}
static int cmd_screenshot(struct konsole* ks, int argc, char** argv)
{
    if (!orion_display_ready()) {
        kon_printf(ks, "screenshot: display not initialized\r\n");
        return -1;
    }
    const sd_t* sd = sic_sd(0);
    if (!sd || !sd->v) { kon_printf(ks, "screenshot: no SD driver\r\n"); return -1; }
    if (sd->v->begin) (void)sd->v->begin(sd);
    if (!(sd->v->present && sd->v->present(sd) == 1)) {
        kon_printf(ks, "screenshot: no SD card present\r\n");
        return -1;
    }

    const char* name = (argc >= 2 && argv[1][0]) ? argv[1] : "screenshot";
    /* EEXIST is the expected/common case (the directory almost always
     * already exists) and not worth a status line every call; anything
     * else is unusual enough to surface. */
    errno = 0;
    if (mkdir("/sdcard/orion", 0777) != 0 && errno != EEXIST)
        kon_printf(ks, "screenshot: mkdir failed errno=%d (%s)\r\n", errno, strerror(errno));

    size_t stride = 0;
    const uint8_t* fb = (const uint8_t*)orion_fb_ptr(&stride);
    int w = orion_display_phys_w(), h = orion_display_phys_h();
    if (!fb || w <= 0 || h <= 0) { kon_printf(ks, "screenshot: no framebuffer\r\n"); return -1; }

    char ppm_path[80], txt_path[80];
    snprintf(ppm_path, sizeof ppm_path, "/sdcard/orion/%s.ppm", name);
    snprintf(txt_path, sizeof txt_path, "/sdcard/orion/%s.txt", name);

    /* A filename with a partial/interrupted write from an earlier attempt
     * can leave FATFS with a directory entry fopen(..., "wb") refuses to
     * reopen (errno=EINVAL), persisting even across a clean reboot. A
     * plain retry loop can't fix a persistently bad entry; removing it
     * first can, since create-after-remove is a normal, well-supported
     * FATFS operation. (sd_sdmmc_begin() already returns immediately when
     * already mounted, so this isn't a mount-timing race.) */
    FILE* fp = NULL;
    for (int attempt = 0; attempt < 3 && !fp; ++attempt) {
        if (attempt > 0) { (void)remove(ppm_path); orion_delay_ms(200); }
        errno = 0;
        fp = fopen(ppm_path, "wb");
    }
    if (!fp) {
        kon_printf(ks, "screenshot: fopen(%s) failed errno=%d (%s)\r\n", ppm_path, errno, strerror(errno));
        return -1;
    }
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    uint8_t row[3 * 1280];   /* 1280 covers the wider of phys_w/phys_h either orientation */
    for (int y = 0; y < h; ++y) {
        const uint16_t* px = (const uint16_t*)(fb + (size_t)y * stride);
        for (int x = 0; x < w; ++x) {
            uint16_t v = px[x];
            row[x*3+0] = (uint8_t)((((v>>11)&31)*255+15)/31);
            row[x*3+1] = (uint8_t)((((v>>5)&63)*255+31)/63);
            row[x*3+2] = (uint8_t)(((v&31)*255+15)/31);
        }
        fwrite(row, 1, (size_t)w * 3, fp);
    }
    fclose(fp);

    /* Layout sidecar: real, live state -- not hand-transcribed guesses.
     * Marquee registry entries are exactly the rects/text the app itself
     * currently believes are on screen and animating (see
     * orion_ui_text_marquee()) -- genuinely useful ground truth for
     * spotting a rect that's wrong or a redraw that's touching more (or
     * less) than it should. */
    FILE* tp = fopen(txt_path, "w");
    if (tp) {
        const orion_ui_theme_t* th = orion_ui_theme();
        fprintf(tp, "logical   : %dx%d\n", orion_display_w(), orion_display_h());
        fprintf(tp, "physical  : %dx%d (this screenshot's own dimensions)\n", w, h);
        fprintf(tp, "rotation  : %d (0=portrait 1=landscape-cw 2=upside-down 3=landscape-ccw)\n",
                orion_display_get_rotation());
        fprintf(tp, "panel     : %s\n", orion_panel_name());
        fprintf(tp, "backlight : %d%%\n", orion_backlight_get());
        fprintf(tp, "theme     : %s  ui_scale_step=%d  grid_cols=%d\n",
                th->name, orion_ui_get_scale(), orion_ui_get_grid_cols());
        fprintf(tp, "metrics   : margin=%d gap=%d topbar_h=%d card_h=%d row_h=%d touch_min=%d\n",
                th->metrics.margin, th->metrics.gap, th->metrics.topbar_h,
                th->metrics.card_h, th->metrics.row_h, th->metrics.touch_min);
        int mc = orion_ui_marquee_debug_count();
        fprintf(tp, "marquee_registry: %d entries currently overflowing/animating\n", mc);
        for (int i = 0; i < mc; ++i) {
            orion_ui_rect_t r; char text[80];
            orion_ui_marquee_debug_get(i, &r, text, sizeof text);
            fprintf(tp, "  [%2d] x=%-4d y=%-4d w=%-4d text=\"%s\"\n", i, r.x, r.y, r.w, text);
        }
        fclose(tp);
    }

    kon_printf(ks, "screenshot: wrote %s (%dx%d) + %s\r\n", ppm_path, w, h, txt_path);
    return 0;
}
static size_t xfer_io_console_read(void* ctx, uint8_t* buf, size_t len)
{ (void)ctx; return orion_console_read(buf, len); }
static size_t xfer_io_console_write(void* ctx, const uint8_t* buf, size_t len)
{ (void)ctx; return orion_console_write(buf, len); }
static const orion_xfer_io_t kConsoleXferIo = { xfer_io_console_read, xfer_io_console_write, NULL };

static int cmd_send(struct konsole* ks, int argc, char** argv)
{
    if (argc < 2) { kon_printf(ks, "usage: send <path>\r\n"); return -1; }
    kon_printf(ks, "send: waiting for YMODEM receiver on this same connection...\r\n");
    orion_xfer_result_t rc = orion_ymodem_send(&kConsoleXferIo, argv[1], NULL);
    kon_printf(ks, "send: rc=%d\r\n", (int)rc);
    return rc == ORION_XFER_OK ? 0 : -1;
}
static int cmd_recv(struct konsole* ks, int argc, char** argv)
{
    const char* dest = (argc >= 2 && argv[1][0]) ? argv[1] : "/sdcard";
    kon_printf(ks, "recv: waiting for YMODEM sender on this same connection, into %s...\r\n", dest);
    char name[96] = {0};
    orion_xfer_result_t rc = orion_ymodem_receive(&kConsoleXferIo, dest, name, sizeof name, NULL);
    kon_printf(ks, "recv: rc=%d name=%s\r\n", (int)rc, name);
    return rc == ORION_XFER_OK ? 0 : -1;
}
static int cmd_bl(struct konsole* ks, int argc, char** argv)
{
    if (argc < 2) {
        kon_printf(ks, "backlight: %d%%   (usage: bl <0-100>)\r\n",
                   orion_backlight_get());
        return 0;
    }
    int pct = (int)strtol(argv[1], NULL, 10);
    int rc  = orion_backlight_set(pct);
    kon_printf(ks, "backlight: %d%% rc=%d\r\n", orion_backlight_get(), rc);
    return rc;
}
static int cmd_ls(struct konsole* ks, int argc, char** argv)
{
    const char* dir = (argc >= 2 && argv[1][0]) ? argv[1] : "/sdcard";
    DIR* dp = opendir(dir);
    if (!dp) {
        kon_printf(ks, "ls: cannot open %s (errno=%d)\r\n", dir, errno);
        return -1;
    }
    struct dirent* e;
    while ((e = readdir(dp)) != NULL) {
        char full[512];
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            kon_printf(ks, "  <dir>  %s\r\n", e->d_name);
        } else if (stat(full, &st) == 0) {
            kon_printf(ks, "  %8ld  %s\r\n", (long)st.st_size, e->d_name);
        } else {
            kon_printf(ks, "  ?      %s\r\n", e->d_name);
        }
    }
    closedir(dp);
    return 0;
}

/* --- midi -- manual send test, no touchscreen needed --- */

static int cmd_midi(struct konsole* ks, int argc, char** argv)
{
    if (!orion_usb_midi_ready()) {
        kon_printf(ks, "midi: device not active\r\n");
        return -1;
    }
    kon_printf(ks, "midi: %s\r\n",
               orion_usb_midi_host_connected() ? "host connected" : "no host (enumerated, cable end idle)");

    if (argc >= 2 && strcmp(argv[1], "note") == 0) {
        uint8_t note = (argc >= 3) ? (uint8_t)(strtol(argv[2], NULL, 10) & 0x7F) : 60;
        uint8_t on[3]  = { 0x90, note, 100 };
        uint8_t off[3] = { 0x80, note, 0 };
        int rc = orion_usb_midi_send(on);
        kon_printf(ks, "midi: note on  %u rc=%d\r\n", (unsigned)note, rc);
        orion_delay_ms(200);
        rc = orion_usb_midi_send(off);
        kon_printf(ks, "midi: note off %u rc=%d\r\n", (unsigned)note, rc);
        return 0;
    }
    kon_printf(ks, "usage: midi note [0-127]   (default 60 / middle C)\r\n");
    return 0;
}

/* --- perf -- one-off diagnostic to find out WHERE the ~14.3 FPS cap
 * actually comes from: draw cost (lp_draw_frame(), which scales with each
 * mode's fill_rect call count, per the native-preview counter) or present
 * cost (sgfx_present() -> orion_dsi_present() -> sgfx_hal_dsi_flip(),
 * which cache-syncs the WHOLE framebuffer then blocks on the DSI panel's
 * own on_refresh_done ISR semaphore -- a cost that does NOT depend on
 * what was drawn at all). Calls lp_draw_frame()+sgfx_present() directly
 * in a tight loop, bypassing the touch loop and its LP_PANEL_FRAME_MS
 * pacing gate entirely, so this measures the real unthrottled cost. Not
 * meant to ship long-term -- remove once the question is settled. */
static int cmd_perf(struct konsole* ks, int argc, char** argv)
{
    (void)argc; (void)argv;
    sgfx_device_t* d = orion_gfx();
    if (!d) { kon_printf(ks, "perf: display not initialized\r\n"); return -1; }
    int w = orion_display_w(), h = orion_display_h();
    const int N = 15;

    struct { const char* name; lp_grid_model_t m; } cases[4];
    memset(cases, 0, sizeof cases);
    cases[0].name = "notes (high fill_rect count)";
    cases[0].m.mode = LP_MODE_NOTE;
    cases[0].m.midi_active = 1; cases[0].m.host_connected = 1;
    cases[1].name = "xy (low fill_rect count)";
    cases[1].m.mode = LP_MODE_XY_MACRO;
    cases[1].m.midi_active = 1; cases[1].m.host_connected = 1;
    cases[1].m.xy_valid = 1; cases[1].m.xy_x = w/2; cases[1].m.xy_y = h/2;
    cases[2].name = "looper (4-track screen)";
    cases[2].m.mode = LP_MODE_LOOPER;
    cases[2].m.midi_active = 1; cases[2].m.host_connected = 1;
    cases[3].name = "faders (dragging, like the user's test)";
    cases[3].m.mode = LP_MODE_MIXER_CC;
    cases[3].m.midi_active = 1; cases[3].m.host_connected = 1;
    cases[3].m.mixer_value[0] = 64; cases[3].m.mixer_value[1] = 90;
    cases[3].m.mixer_value[2] = 30; cases[3].m.mixer_value[3] = 110;
    cases[3].m.mixer_touch_active[1] = 1;

    for (int c = 0; c < 4; ++c) {
        int64_t draw_sum = 0, present_sum = 0;
        int64_t draw_min = INT64_MAX, draw_max = 0;
        int64_t present_min = INT64_MAX, present_max = 0;
        for (int i = 0; i < N; ++i) {
            int64_t t0 = esp_timer_get_time();
            lp_draw_frame(d, w, h, &cases[c].m);
            int64_t t1 = esp_timer_get_time();
            sgfx_present(d);
            int64_t t2 = esp_timer_get_time();
            int64_t dd = t1 - t0, dp = t2 - t1;
            draw_sum += dd; present_sum += dp;
            if (dd < draw_min) draw_min = dd;
            if (dd > draw_max) draw_max = dd;
            if (dp < present_min) present_min = dp;
            if (dp > present_max) present_max = dp;
        }
        kon_printf(ks, "%-28s draw avg=%lldus min=%lldus max=%lldus | present avg=%lldus min=%lldus max=%lldus\r\n",
                   cases[c].name,
                   (long long)(draw_sum / N), (long long)draw_min, (long long)draw_max,
                   (long long)(present_sum / N), (long long)present_min, (long long)present_max);
    }

    /* Control: minimal draw (one small fill), isolates present()'s own
     * floor cost (cache sync + flip + refresh-done wait) from any grid
     * rendering at all. */
    {
        int64_t draw_sum = 0, present_sum = 0;
        for (int i = 0; i < N; ++i) {
            int64_t t0 = esp_timer_get_time();
            sgfx_fill_rect(d, i % 4, 0, 4, 4, (sgfx_rgba8_t){ (uint8_t)i, 0, 0, 255 });
            int64_t t1 = esp_timer_get_time();
            sgfx_present(d);
            int64_t t2 = esp_timer_get_time();
            draw_sum += (t1 - t0); present_sum += (t2 - t1);
        }
        kon_printf(ks, "%-28s draw avg=%lldus                      | present avg=%lldus\r\n",
                   "minimal (1 fill_rect)", (long long)(draw_sum / N), (long long)(present_sum / N));
    }
    return 0;
}

/* --- table --- */

const struct kon_cmd ORION_CMDS[] = {
    { "sys",        "system + chip info",                      cmd_sys },
    { "mem",        "internal/PSRAM free + largest + min-ever", cmd_mem },
    { "hw",         "list detected SIC hardware",               cmd_hw },
    { "panel",      "detected display/touch panel",             cmd_panel },
    { "i2c",        "i2c [0|1] bus scan",                       cmd_i2c },
    { "touch",      "live touch points (key=exit)",             cmd_touch },
    { "rtc",        "rtc [set YYYY MM DD HH MM SS]",            cmd_rtc },
    { "imu",        "live accel/gyro (key=exit)",               cmd_imu },
    { "bat",        "battery + charger status",                 cmd_bat },
    { "sd",         "mount SD and report card info",            cmd_sd },
    { "ioexp",      "ioexp <exp> <bit> [0|1]",                  cmd_ioexp },
    { "gfx",        "gfx [info|splash]",                        cmd_gfx },
    { "bl",         "bl [0-100] backlight percent",             cmd_bl },
    { "midi",       "midi [note [0-127]] -- status / send test", cmd_midi },
    { "screenshot", "screenshot [name] -- dumps SD:/orion/<name>.ppm+.txt", cmd_screenshot },
    { "send",       "send <path> -- YMODEM-send a file over this connection", cmd_send },
    { "recv",       "recv [destdir] -- YMODEM-receive a file over this connection", cmd_recv },
    { "ls",         "ls [dir] -- list files (temporary diagnostic)", cmd_ls },
    { "perf",       "draw vs present timing breakdown per mode (temporary diagnostic)", cmd_perf },
};
const size_t ORION_CMD_COUNT = sizeof(ORION_CMDS) / sizeof(ORION_CMDS[0]);
