/*
 * main.c — Tab5-Launchpad entry point and bring-up order.
 *
 * Forked from Tab5-Orion's main.c; see orion.h's header comment for what
 * carried over and what didn't.
 *
 * ── Bring-up order ──────────────────────────────────────────────────────
 *   1. console   — first, so every later step has somewhere to report to.
 *   2. konsole   — the CLI, over an io bridge on that console.
 *   3. SIC       — sic_begin_legacy(&SIC_BOARD_TAB5, NULL), board preinit
 *                  (I2C bus 0, panel/touch identity, backlight power rails).
 *   4. SGFX      — must come after step 3 (panel choice reads SIC's cached
 *                  panel identity).
 *   5. USB-MIDI  — installed here, once, permanently (this firmware has no
 *                  USB host mode to switch away from -- see
 *                  usb_midi_device.h).
 *   6. banner, splash, then the shell.
 */

#include "orion.h"

#include <stdio.h>
#include <string.h>

#include "esp_idf_version.h"
#include "esp_system.h"

#include "konsole/static.h"

#include "sic/sic.h"
#include "sic/bus/i2c_bus.h"
#include "boards/tab5/ioexpander.h"

#include "ui_theme.h"
#include "settings_store.h"
#include "usb_midi_device.h"

#include "esp_log.h"

/* ── konsole instance + io bridge ──────────────────────────────────────── */

struct konsole               g_ks;
static struct kon_line_state g_line;

static size_t io_read_avail(void* ctx) { (void)ctx; return 1024; }
static size_t io_read(void* ctx, uint8_t* buf, size_t len) { (void)ctx; return orion_console_read(buf, len); }
static size_t io_write(void* ctx, const uint8_t* buf, size_t len) { (void)ctx; return orion_console_write(buf, len); }
static uint32_t io_millis(void* ctx) { (void)ctx; return orion_millis(); }

/* konsole's built-in `reboot` command calls this weak hook. */
void konsole_on_reboot(void)
{
    esp_restart();
}

/* ── banner ────────────────────────────────────────────────────────────── */

static void print_banner(void)
{
    kon_printf(&g_ks, "\r\n=== Tab5-Launchpad %s ===\r\n", ORION_VERSION);
    kon_printf(&g_ks, "build   : %s %s (IDF %s)\r\n",
               __DATE__, __TIME__, esp_get_idf_version());
    kon_printf(&g_ks, "console : %s\r\n", orion_console_name());
    kon_printf(&g_ks, "panel   : %s%s\r\n", orion_panel_name(),
               orion_display_ready() ? "" : "  (display init FAILED)");
    if (orion_display_ready())
        kon_printf(&g_ks, "screen  : %dx%d RGB565, backlight %d%%\r\n",
                   orion_display_w(), orion_display_h(), orion_backlight_get());

    kon_printf(&g_ks, "hardware:");
    int any = 0;
    for (int f = 0; f < (int)SIC_F__COUNT; ++f) {
        sic_func_id_t fn = (sic_func_id_t)f;
        int n = sic_count_fn(fn);
        if (n <= 0) continue;
        kon_printf(&g_ks, " %s(%s)", sic_func_name(fn), sic_name_fn(fn, 0));
        any = 1;
    }
    kon_printf(&g_ks, "%s\r\n", any ? "" : " none detected");
    kon_printf(&g_ks, "usb-midi: %s\r\n", orion_usb_midi_ready() ? "active" : "FAILED to install");
    kon_banner(&g_ks, "ready - type 'help' for commands");
}

/* ── entry point ───────────────────────────────────────────────────────── */

void app_main(void)
{
    orion_canary_register_main_task();
    orion_ui_theme_reset_default();

    /* 1. console */
    orion_console_init();

    /* 2. konsole */
    struct konsole_io io = {
        .read_avail = io_read_avail,
        .read       = io_read,
        .write      = io_write,
        .millis     = io_millis,
        .ctx        = NULL,
    };
    konsole_init_with_storage(&g_ks, &g_line, &io,
                              ORION_CMDS, ORION_CMD_COUNT,
                              "launchpad> ", /*vt100*/ true);

    /* 3. SIC */
    int rc = sic_i2c_begin_bus(TAB5_I2C_BUS, TAB5_I2C_SDA, TAB5_I2C_SCL, TAB5_I2C_HZ);
    kon_printf(&g_ks, "[SIC] i2c bus %d SDA=%d SCL=%d @%uHz rc=%d\r\n",
               TAB5_I2C_BUS, TAB5_I2C_SDA, TAB5_I2C_SCL, (unsigned)TAB5_I2C_HZ, rc);

    rc = sic_begin_legacy(&SIC_BOARD_TAB5, NULL);
    kon_printf(&g_ks, "[SIC] board=%s begin rc=%d\r\n", SIC_BOARD_TAB5.name, rc);

    /* 4. SGFX */
    orion_display_init(&g_ks);

    /* Persisted settings hot-load -- after display init, so backlight/
     * auto-rotation setters have real hardware state to override. */
    rc = orion_settings_load_sd();
    kon_printf(&g_ks, "[CFG] settings load rc=%d\r\n", rc);

    rc = orion_ui_theme_reload_sd();
    kon_printf(&g_ks, "[THEME] SD theme load rc=%d\r\n", rc);

    /* USB5V_EN (IO-expander 1, bit 3) is driven HIGH by SIC's generic Tab5
     * board bring-up above (tab5_ioexp_init(), inside sic_begin_legacy())
     * -- a holdover meant for USB HOST mode, where the Tab5 needs to power
     * a connected keyboard/gamepad on this same port. This firmware never
     * acts as a host on this port (it's a permanent USB-MIDI DEVICE, see
     * usb_midi_device.h) -- in device mode the Tab5 must NOT be sourcing
     * its own 5V onto VBUS; the HOST (the computer it's plugged into) is
     * the one that's supposed to drive VBUS, and a compliant device should
     * stay high-impedance there and only sense it. Leaving USB5V_EN high
     * means both ends are actively driving the same VBUS line at once,
     * which is a well-known real-world cause of a host silently refusing
     * to enumerate a downstream device (contention/overcurrent protection
     * tripping before USB-level negotiation even starts) -- exactly the
     * "shows up as nothing at all, not even a generic USB device" symptom
     * reported the first time this was tested against a real host this
     * session. Must run before orion_usb_midi_init() below, so VBUS is
     * already clean before TinyUSB starts advertising attachment. */
    tab5_ioexp_set(1, 3, 0);

    /* 5. USB-MIDI -- permanently active from here on, see usb_midi_device.h. */
    rc = orion_usb_midi_init();
    kon_printf(&g_ks, "[MIDI] usb-midi init rc=%d\r\n", rc);

    /* 6. Banner, splash, then the shell. The shell keeps konsole_poll()
     * alive internally, so USB/UART console input remains usable while
     * the touchscreen UI is up. */
    print_banner();
    orion_display_splash();
    orion_delay_ms(1000);
    orion_shell_run(&g_ks);

    for (;;) {
        konsole_poll(&g_ks);
        orion_delay_ms(1);
    }
}
