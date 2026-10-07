/*
 * panel_detect.h — Tab5-private one-time display/touch panel identification.
 *
 * Tab5 ships with one of three possible display+touch chip pairings; which
 * one is present can only be learned via I2C probes, and SIC's design
 * invariant (docs/DESIGN_INVARIANTS.md, "Probe never does I/O") forbids
 * driver probe() functions from doing that I/O themselves. So the actual
 * hardware probing happens once here, at board preinit() time, and the
 * result is cached for the touch driver's probe() calls to read back
 * cheaply.
 *
 * Board-private plumbing, not public SIC API — reached via a local include
 * path ("boards/tab5/panel_detect.h"), never via the public sic/ namespace.
 *
 * Ordering dependency: tab5_panel_detect() runs on I2C bus 0 and requires
 * that bus to already be open with LCD_RST/TP_RST already released, which
 * only tab5_ioexp_init() (ioexpander.c) does. Board preinit() is responsible
 * for calling tab5_ioexp_init() BEFORE tab5_panel_detect() — this header
 * does not (and cannot) enforce that ordering itself.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TAB5_PANEL_UNKNOWN = 0,
    TAB5_PANEL_ILI9881C_GT911,
    TAB5_PANEL_ST7121,
    TAB5_PANEL_ST7123,
} tab5_panel_kind_t;

/*
 * Runs the panel/touch identification probe sequence on I2C bus 0, exactly
 * once (idempotent: a no-op if a result is already cached). Requires bus 0
 * to already be open and LCD_RST/TP_RST already released — call
 * tab5_ioexp_init() first. See panel_detect.c for the exact probe sequence.
 */
void tab5_panel_detect(void);

/*
 * Returns the cached detection result. TAB5_PANEL_UNKNOWN if
 * tab5_panel_detect() has not been called yet.
 */
tab5_panel_kind_t tab5_panel_detected(void);

#ifdef __cplusplus
}
#endif
