/*
 * orion.h — Tab5-Launchpad internal interfaces.
 *
 * Forked from Tab5-Orion (the M5Stack Tab5 multi-app launcher/retro-console
 * this project started from): its reusable HAL (lib/SIC), graphics core
 * (lib/SGFX) and console library (lib/konsole), plus this generic
 * display/console/theme/widget infrastructure, are carried over verbatim.
 * Everything Orion-specific (the multi-app launcher, Doom/Quake/Wolf3D,
 * the video player, dynamic ELF apps, USB host mode, WiFi/BT) is NOT
 * carried over -- this is a dedicated, from-scratch firmware whose only
 * real feature is the Launchpad (touchscreen grid MIDI controller), with
 * a small shell around it (home screen, Settings, Diagnostics).
 *
 * Core translation units, no hardware abstraction layer between them and ESP-IDF:
 *   console.c   console transport (USB-Serial-JTAG or UART, per sdkconfig)
 *   display.c   MIPI-DSI bring-up with runtime panel selection + backlight
 *   commands.c  the konsole command table
 *   shell.c     home screen + screen dispatch
 *   ui_*.c      portable theme/components/screens shared with the host preview
 *   main.c      app_main(): bring-up order + poll loop
 *
 * Deliberately NOT a portability layer. This firmware targets ESP-IDF on
 * Tab5, so the functions below are thin named wrappers over ESP-IDF calls,
 * not an indirection with swappable implementations.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "konsole/konsole.h"
#include "sgfx.h"

#ifndef ORION_VERSION
#define ORION_VERSION "0.0.0-dev"
#endif

/* ── console.c ─────────────────────────────────────────────────────────── */

/* Install the console driver selected by sdkconfig. 0 on success. */
int         orion_console_init(void);
/* Human-readable name of the transport actually compiled in. */
const char* orion_console_name(void);

/* Non-blocking. Returns bytes actually transferred (0 if none / not ready). */
size_t orion_console_write(const uint8_t* buf, size_t len);
size_t orion_console_read(uint8_t* buf, size_t len);
/* Feed bytes into the same input stream as USB/UART (on-screen/physical keyboard). */
size_t orion_console_inject(const uint8_t* buf, size_t len);

/* Non-blocking single byte, or -1 if nothing is buffered. The "any key exits"
 * escape hatch every live/looping test command uses. */
int  orion_console_getch(void);
/* Discard whatever is already buffered — call before entering a loop so the
 * Enter that launched the command does not immediately exit it. */
void orion_console_drain(void);

uint32_t orion_millis(void);
void     orion_delay_ms(uint32_t ms);

/* Main-task stall canary -- see console.c's orion_delay_ms() comment for
 * how it's kicked. Call orion_canary_register_main_task() once, early,
 * from the task that runs orion_touchmenu_run() (main.c's app_main()).
 * orion_canary_age_ms() is how long it's been since that task last proved
 * forward progress -- kbd_accessory.c's LED heartbeat uses this to flag a
 * UI stall the same way it already flags a bad I2C link. */
void     orion_canary_register_main_task(void);
uint32_t orion_canary_age_ms(void);

/* ── display.c ─────────────────────────────────────────────────────────── */

/* Select the DSI panel from SIC's cached tab5_panel_detected(), bring up the
 * bus, init the panel, turn the backlight on. MUST run after sic_begin_legacy().
 * Logs progress through `ks`. 0 on success. */
int orion_display_init(struct konsole* ks);

int         orion_display_ready(void);
const char* orion_panel_name(void);
sgfx_device_t* orion_gfx(void);          /* NULL until orion_display_init() succeeds */
void*       orion_fb_ptr(size_t* stride_bytes);
int         orion_display_w(void);
int         orion_display_h(void);

/* ── Landscape UI rotation ─────────────────────────────────────────────
 * The DSI panel drivers can't rotate scanout (ili9881c_set_rotation()
 * returns SGFX_ERR_NOSUP -- no verified-safe MADCTL-equivalent for a
 * video-mode DSI stream); this is a software rotation applied at the single
 * point every draw call funnels through (orion_dsi_fill_rect()), so
 * touchmenu.c's ~30+ screens need zero changes to their own drawing code --
 * they already lay out generically against orion_display_w()/h(), which
 * become rotation-aware (swapped dims for 90°/270°) below.
 *
 * rot: 0=portrait (native), 1=landscape (CW), 2=upside-down, 3=landscape (CCW).
 */
void orion_display_set_rotation(int rot);
int  orion_display_get_rotation(void);

/* Save/restore, for screens that must stay pinned to portrait regardless of
 * physical device tilt (the IMU test screen, whose bubble-level is deliberately
 * not rotation-aware) -- push at screen entry, pop at exit. Nestable up to a
 * small fixed depth. */
void orion_display_push_rotation(int rot);
void orion_display_pop_rotation(void);

/* Physical (always 720x1280, real scanout buffer) vs logical (rotation-aware,
 * what orion_display_w()/h() return) dimensions -- for the few call sites that
 * write raw pixels straight into orion_fb_ptr()'s buffer instead of going
 * through sgfx_fill_rect() (the mouse cursor sprite, orion_fb_fill_circle()):
 * they must transform their own logical (x,y,w,h) through this before writing,
 * since orion_dsi_fill_rect() only sees calls that already went through SGFX. */
int  orion_display_phys_w(void);
int  orion_display_phys_h(void);
void orion_display_logical_to_phys(int lx, int ly, int lw, int lh,
                                   int* px, int* py, int* pw, int* ph);

/* Inverse (point-only) -- physical touch coordinates to logical, used by the
 * touch input shim (touchmenu.c) so every screen's hit-testing sees logical
 * coordinates without needing to know rotation exists. */
void orion_display_phys_to_logical(int px, int py, int* lx, int* ly);

/* Reads the IMU, applies a debounce window, and calls
 * orion_display_set_rotation() when a new orientation has been held long
 * enough. Cheap and safe to call every poll iteration -- no-op if there's
 * no IMU. The app shell and the new first-class apps poll it continuously;
 * diagnostics that pin a rotation use push/pop to opt out safely. */
void orion_display_poll_auto_rotation(void);
void orion_display_set_auto_rotation(int enabled);
int  orion_display_auto_rotation_enabled(void);

/* Double-buffering -- see display.c's definitions and
 * sgfx_hal.h's sgfx_hal_dsi_get_back_fb()/sgfx_hal_dsi_flip() for the
 * mechanism and ownership rule. */
void* orion_video_get_back_fb(size_t* stride_bytes);
int   orion_video_flip(uint32_t timeout_ms);
int   orion_video_flip_hw_written(uint32_t timeout_ms);
int   orion_video_flip_to_front(uint32_t timeout_ms);

/* Backlight PWM (LEDC low-speed timer 0 / channel 1 on GPIO22). Set by
 * orion_display_init(); exposed for the `bl` command. */
int orion_backlight_set(int percent);
int orion_backlight_get(void);
/* True full-off (duty=0, no floor), for idle
 * sleep only -- see display.c's own comment on orion_backlight_set() for
 * why the normal slider path can't go this low. Does not change what
 * orion_backlight_get() reports. */
int orion_backlight_off_full(void);

/* Boot splash: panel identity + detected SIC capabilities, drawn on screen. */
void orion_display_splash(void);

/* ── commands.c ────────────────────────────────────────────────────────── */

extern const struct kon_cmd ORION_CMDS[];
extern const size_t         ORION_CMD_COUNT;

/* Shared by commands.c and touchmenu.c: the console instance to print to. */
extern struct konsole g_ks;

/* ── shell.c / app shell ───────────────────────────────────────────────── */

/* Run the standalone touchscreen shell: a small 3-tile home screen
 * (Launchpad / Settings / Diagnostics). Normally does not return; the
 * serial console remains live inside the loop. Returns <0 only if the
 * display or touch input is unavailable. */
int orion_shell_run(struct konsole* ks);
