/*
 * launchpad_app.h — Launchpad screen entry point.
 *
 * This is the one ESP-IDF-specific file in the feature (owns the touch
 * poll loop and the real USB-MIDI calls); launchpad_grid.c/
 * launchpad_modes.c underneath it are deliberately platform-free.
 */
#pragma once

#include "sgfx.h"
#include "sic/input/touch.h"

#ifdef __cplusplus
extern "C" {
#endif

struct konsole;

/* Same calling convention as the other shell screens (shell.c). Returns 0
 * on a normal "home" exit. USB-MIDI is already active by the time this is
 * called (initialized once at boot, see usb_midi_device.h) -- this
 * function just reads its state for the status indicator and sends
 * messages through it. */
int run_launchpad_app(sgfx_device_t* d, const touch_t* t, struct konsole* ks);

#ifdef __cplusplus
}
#endif
