#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ORION_SETTINGS_DEFAULT_SD_PATH "/sdcard/orion/settings.ini"

/* Persists the handful of user-facing preferences that have no other home
 * (UI-size step, home-screen grid columns, backlight %, auto-rotation) to
 * an SD card, mirroring the theme engine's own "no SD required, optional
 * override" contract (ui_theme.c/THEMING.md): a missing or invalid file
 * changes nothing, current in-memory values (compiled defaults, or
 * whatever's already been set this boot) stay exactly as they were.
 * Deliberately does NOT cover theme selection -- that's already its own
 * separate, explicitly-manual "tap Reload from SD" flow (see
 * orion_ui_theme_reload_sd()); auto-applying a possibly-edited theme file
 * on every single boot is a different, riskier decision than persisting
 * these plain numeric/boolean settings, and wasn't asked for. */

/* Call once at boot, after the subsystems that own each setting (display/
 * backlight, UI theme) have already initialized to their compiled
 * defaults -- this only overwrites them if a valid file is actually
 * present. Returns 0 on a successful load, negative if no card/file/valid
 * data was found (all non-fatal; caller should keep booting either way). */
int orion_settings_load_sd(void);

/* Call after any of the covered settings actually changes. No-op (returns
 * negative, not treated as an error by callers) if no SD card is mounted
 * right now -- this never queues a retry; the next real change will just
 * try again. Cheap enough to call on every discrete change (a toggle tap,
 * a selector tap, a completed drag) but callers should NOT call this on
 * every intermediate value of a live drag (e.g. per-pixel backlight
 * dragging) -- save once the drag/gesture settles, same restraint already
 * used for other redraw-discipline fixes, just applied to SD
 * writes instead of screen redraws. */
int orion_settings_save_sd(void);

#ifdef __cplusplus
}
#endif
