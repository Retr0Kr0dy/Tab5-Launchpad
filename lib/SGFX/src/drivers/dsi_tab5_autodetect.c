#ifdef SGFX_DRV_TAB5_AUTO

#include "sgfx.h"
#include "sgfx_hal.h"
#include <stddef.h>

/*
 * dsi_tab5_autodetect.c — Tab5-only runtime panel autodetect meta-driver.
 *
 * Tab5 ships with one of three DSI panel ICs (ILI9881C, ST7121, ST7123),
 * chosen at manufacturing time and not otherwise discoverable ahead of
 * bringing the DSI bus up -- the real identity check M5Stack's own firmware
 * performs is a *touch-controller* FW-version read at I2C address 0x55
 * (SIC's job, in the sibling repo: SGFX
 * display bring-up must run before SIC's touch probe on combo chips since
 * they share the LCD_RST/TP_RST reset line). This bus's DSI backend
 * (espidf_dsi.c) also leaves bus->ops->read_data NULL -- there is no DCS
 * register-readback path available at this layer either.
 *
 * So this meta-driver takes the only identity signal actually available to
 * SGFX: whether a given panel's real DCS init sequence is accepted by the
 * transport (write_cmd/write_data/esp_lcd_panel_io_tx_param all succeed) --
 * try each real panel's init() in turn, keep whichever one doesn't fail.
 * This is safe specifically because init() -- unlike SIC's probe() -- is the
 * documented place a SGFX driver first touches real hardware; there is no
 * "no I/O before commit" invariant to violate here.
 *
 * Build note: a Tab5 SGFX_DRV_TAB5_AUTO build must also compile
 * ili9881c.c / st7121.c / st7123.c, i.e. define all four of
 * SGFX_DRV_ILI9881C, SGFX_DRV_ST7121, SGFX_DRV_ST7123 and
 * SGFX_DRV_TAB5_AUTO together via build_flags -- this file only calls
 * into their ops tables, it does not compile them in on its own.
 */

extern const sgfx_driver_ops_t sgfx_ili9881c_ops;
extern const sgfx_driver_ops_t sgfx_st7121_ops;
extern const sgfx_driver_ops_t sgfx_st7123_ops;

#define TAB5_AUTO_W 720
#define TAB5_AUTO_H 1280

static int tab5_auto_init(sgfx_device_t* d){
  /* Try each real panel's init() directly through its own ops table --
   * *not* through d->drv, which still points at this meta-driver's own ops
   * table for the duration of this call. Whichever succeeds, reassign
   * d->drv to that panel's real (const) ops table so every subsequent call
   * on this device (present/get_fb_ptr/flush_surface/...) goes straight to
   * the real driver with zero indirection through this file ever again. */
  if (sgfx_ili9881c_ops.init(d) == SGFX_OK){ d->drv = &sgfx_ili9881c_ops; return SGFX_OK; }
  if (sgfx_st7121_ops.init(d)   == SGFX_OK){ d->drv = &sgfx_st7121_ops;   return SGFX_OK; }
  if (sgfx_st7123_ops.init(d)   == SGFX_OK){ d->drv = &sgfx_st7123_ops;   return SGFX_OK; }
  return SGFX_ERR_NOSUP;
}

/*
 * Every other member is deliberately NULL/absent: once tab5_auto_init()
 * returns SGFX_OK, d->drv no longer points at this table at all, so nothing
 * else here is ever called. (init is the sole entry point a caller can reach
 * through this ops table, via sgfx_init()/sgfx_open_dsi() calling
 * dev->drv->init(dev) before anything else touches dev->drv.)
 */
const sgfx_driver_ops_t sgfx_tab5_auto_ops = {
  .init = tab5_auto_init,
};

/* All three real Tab5 panels share width/height/native_fmt/bpp/caps -- only
 * their DCS init sequences and DSI lane speed differ -- so mirroring any one
 * of them here is exact, not approximate. sgfx_autoinit() (sgfx_port.h)
 * overwrites .width/.height from SGFX_W/SGFX_H regardless, so those two
 * fields don't even need to matter here. */
const sgfx_caps_t sgfx_tab5_auto_caps_default = {
  .width      = TAB5_AUTO_W,
  .height     = TAB5_AUTO_H,
  .native_fmt = SGFX_FMT_RGB565,
  .bpp        = 16,
  .caps       = SGFX_CAP_DSI,
};

#endif /* SGFX_DRV_TAB5_AUTO */
