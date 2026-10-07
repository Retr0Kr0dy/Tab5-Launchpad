#ifdef SGFX_DRV_ST7121

#include "sgfx.h"
#include "sgfx_hal.h"
#include <stdint.h>
#include <stddef.h>

/*
 * st7121.c — M5Stack Tab5 ST7121 MIPI-DSI panel driver.
 *
 * Same video-mode-DSI shape as ili9881c.c / st7123.c in this directory. See
 * ili9881c.c's file header for the general rationale (init pushes DCS over
 * write_cmd/write_data then starts DPI video; get_fb_ptr/flush_surface are
 * thin HAL passthroughs), not repeated here.
 *
 * DCS init sequence transcribed from M5Stack's own official Tab5 reference
 * firmware (read-only reference, never linked into this repo):
 *   M5Tab5-UserDemo/platforms/tab5/components/esp_lcd_st7121/esp_lcd_st7121.c
 *   (array vendor_specific_init_default)
 * Unlike ST7123, the Tab5 board bring-up (m5stack_tab5.c,
 * bsp_display_new_with_handles_to_st7123(), is_st7121 branch) passes
 * .init_cmds = NULL / .init_cmds_size = 0 for ST7121, i.e. it explicitly
 * asks the component to use this same file-local default table -- so there
 * is only one authoritative copy for this panel, not two like ST7123's.
 * Only the numeric {cmd, data, delay} values were transcribed; the table
 * struct and the loop that walks it below are this driver's own.
 *
 * DPI timing / DSI lane parameters come from that same board bring-up
 * function, which shares one DSI bus config between ST7121 and ST7123
 * (bitrate/clock identical) and only varies the vertical porches by panel:
 *   lane_bit_rate_mbps == 965   (shared w/ ST7123 -- see st7123.c's header
 *                                 for why this differs from the rough
 *                                 hardware-table figure quoted elsewhere)
 *   dpi_clock_freq_mhz == 70
 *   h_size/v_size = 720/1280, hsync=2, hbp=40, hfp=40 (shared w/ ST7123)
 *   vsync=20, vbp=24, vfp=200 (ST7121-specific)
 */

#define ST7121_W 720
#define ST7121_H 1280

/* ---- Own init-table idiom (not the reference's struct/loop) ---- */
typedef struct {
  uint8_t        cmd;
  uint8_t        len;    /* number of parameter bytes, 0 if none */
  const uint8_t* data;   /* NULL when len == 0 */
  uint16_t       delay_ms;
} dsi_init_cmd_t;

static const dsi_init_cmd_t ST7121_INIT[] = {
  {0x60, 3, (const uint8_t[]){0x71,0x21,0xA2}, 0},
  {0x60, 3, (const uint8_t[]){0x71,0x21,0xA3}, 0},
  {0x60, 3, (const uint8_t[]){0x71,0x21,0xA4}, 0},
  {0x78, 1, (const uint8_t[]){0x21}, 0},
  {0x79, 1, (const uint8_t[]){0xEF}, 0},
  {0xA4, 1, (const uint8_t[]){0x31}, 0},
  {0xB7, 6, (const uint8_t[]){0x00,0x00,0x5F,0x5F,0x44,0x1A}, 0},
  {0xB0, 7, (const uint8_t[]){0x22,0x6B,0x11,0x89,0x25,0x43,0x43}, 0},
  {0xBF, 2, (const uint8_t[]){0xA7,0xA7}, 0},
  {0xA5, 2, (const uint8_t[]){0xF0,0x03}, 0},
  {0xD7, 6, (const uint8_t[]){0x10,0x2C,0x14,0x2A,0x80,0x80}, 0},
  {0x90, 7, (const uint8_t[]){0x71,0x23,0x5A,0x20,0x24,0x11,0x21}, 0},
  {0xA3, 39, (const uint8_t[]){
    0x80,0x01,0x8C,0xFF,0x45,0x00,0x00,0x00,0x00,0x00,0x46,0x00,0x00,
    0x1E,0x5C,0x1E,0x80,0x10,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x46,
    0x00,0x00,0x1E,0x5C,0x1E,0x80,0x10,0xEF,0x58,0x00,0x00,0x00,0xFF}, 0},
  {0xA6, 55, (const uint8_t[]){
    0x0A,0x00,0x24,0x71,0x36,0x00,0x00,0x00,0x68,0x68,0x91,0xFF,0x00,0x24,
    0x71,0x37,0x00,0x00,0x00,0x68,0x68,0x91,0xFF,0x00,0x24,0x71,0x00,0x00,
    0x00,0x00,0x68,0x68,0x91,0xFF,0x00,0x2C,0x71,0x00,0x01,0x00,0x00,0x68,
    0x68,0xFF,0xFF,0x00,0x08,0x80,0x08,0x80,0x06,0x00,0x00,0x00,0x00}, 0},
  {0xA7, 60, (const uint8_t[]){
    0x1A,0x1A,0xC0,0x64,0x40,0x04,0x15,0x40,0x00,0x40,0x00,0x68,0x68,0x91,0xFF,
    0x08,0x80,0x64,0x40,0x26,0x37,0x40,0x00,0x00,0x00,0x68,0x68,0x91,0xFF,0x08,
    0x80,0x64,0x40,0x8C,0x9D,0x40,0x00,0x00,0x00,0x68,0x68,0x91,0xFF,0x08,0x80,
    0x64,0x40,0xAE,0xBF,0x00,0x00,0x20,0x00,0x68,0x68,0x91,0xFF,0x08,0x80,0x79}, 0},
  {0xAC, 44, (const uint8_t[]){
    0x1D,0x18,0x19,0x1D,0x18,0x19,0x04,0x1C,0x1D,0x08,0x0A,0x10,0x12,0x0C,0x0E,
    0x14,0x16,0x00,0x1D,0x1D,0x1D,0x1D,0x1D,0x18,0x19,0x1D,0x18,0x19,0x06,0x1C,
    0x1D,0x09,0x0B,0x11,0x13,0x0D,0x0F,0x15,0x17,0x02,0x1D,0x1D,0x1D,0x1D}, 0},
  {0xAD, 25, (const uint8_t[]){
    0x0C,0x40,0x46,0x00,0x07,0x4B,0x4B,0xFF,0xFF,0xF0,0x40,0x0E,0x01,
    0x07,0x42,0x42,0xFF,0xFF,0x01,0x00,0x00,0xFF,0xFF,0xFF,0xFF}, 0},
  {0xAE, 7, (const uint8_t[]){0xF0,0xFF,0x03,0xF0,0xFF,0x03,0x00}, 0},
  {0xB2, 17, (const uint8_t[]){
    0x15,0x19,0x05,0x23,0x49,0x2D,0x03,0x2E,0x5C,0xD2,0xFF,0x10,0x60,0xFD,0x20,0xC0,0x00}, 0},
  {0xE8, 14, (const uint8_t[]){
    0x20,0x60,0x04,0x8E,0x8E,0x3E,0x04,0xDC,0xDC,0x3E,0x06,0xFA,0x26,0x3E}, 0},
  {0x75, 2, (const uint8_t[]){0x03,0x04}, 0},
  {0xE7, 42, (const uint8_t[]){
    0x4B,0x00,0x00,0xBE,0x4B,0x8C,0x20,0x1A,0xF0,0x7D,0x14,0x7D,0x14,0x7D,
    0x14,0x7D,0x14,0xFF,0x00,0x32,0x30,0x73,0x00,0x00,0xC8,0x6A,0xFF,0x5A,
    0x64,0x38,0x88,0x15,0xB1,0x01,0x01,0x64,0x01,0x01,0x7C,0xFF,0x1A,0x51}, 0},
  {0xE1, 2, (const uint8_t[]){0x0C,0x0C}, 0},
  {0xEA, 3, (const uint8_t[]){0x15,0x00,0x01}, 0},
  {0xC8, 37, (const uint8_t[]){
    0x00,0x00,0x04,0x08,0x10,0x00,0x1F,0x01,0x39,0x3E,0x00,0x78,0x06,
    0xE2,0x02,0x11,0x33,0x01,0x7A,0x0D,0x21,0xC4,0x0B,0x19,0x08,0x32,
    0xA0,0x08,0x1A,0x0A,0xF3,0x7F,0x0E,0xC5,0xE8,0x03,0xFF}, 0},
  {0xC9, 37, (const uint8_t[]){
    0x00,0x00,0x04,0x08,0x10,0x00,0x1F,0x01,0x39,0x3E,0x00,0x78,0x06,
    0xE2,0x02,0x11,0x33,0x01,0x7A,0x0D,0x21,0xC4,0x0B,0x19,0x08,0x32,
    0xA0,0x08,0x1A,0x0A,0xF3,0x7F,0x0E,0xC5,0xE8,0x03,0xFF}, 0},
  {0x60, 3, (const uint8_t[]){0x71,0x21,0x00}, 0},
  /* SLPOUT delay bumped 80ms -> 120ms: the DCS spec minimum settle time
   * after sleep-out, matching what ili9881c.c's verified-against-real-source
   * preamble uses (see that file's fix history). 80ms was untested. */
  {0x11, 0, NULL, 120},
  {0x29, 0, NULL, 800},
  {0x35, 1, (const uint8_t[]){0x00}, 0},
};
#define ST7121_INIT_N (sizeof(ST7121_INIT)/sizeof(ST7121_INIT[0]))

static int run_table(sgfx_device_t* d, const dsi_init_cmd_t* tbl, size_t n){
  for (size_t i = 0; i < n; ++i){
    if (d->bus->ops->write_cmd(d->bus, tbl[i].cmd)) return SGFX_ERR_EIO;
    if (tbl[i].len){
      if (d->bus->ops->write_data(d->bus, tbl[i].data, tbl[i].len)) return SGFX_ERR_EIO;
    }
    if (tbl[i].delay_ms && d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, tbl[i].delay_ms);
  }
  return SGFX_OK;
}

/* ========== Driver ops ========== */

static int st7121_init(sgfx_device_t* d){
  /* ROOT CAUSE FIX (verified against esp_lcd_ili9881c's real source and the
   * m5stack_tab5.c call chain, applied here on the same reasoning since all
   * three Tab5 panels wrap esp_lcd_new_panel_dpi() the same way): with
   * reset_gpio_num == -1 (Tab5's case -- LCD_RST is an I2C GPIO-expander
   * bit this bus can't see), the vendor ST driver's own reset() falls back
   * to a DCS software reset (0x01 SWRESET) + settle delay, called BEFORE
   * init(). Releasing LCD_RST via the expander is not a substitute for
   * this -- it's a different reset than the panel-internal state machine
   * SWRESET clears. This was previously entirely missing (reset() exists
   * on this driver's ops table but nothing in this codebase's sgfx_init()/
   * sgfx_open_dsi() call chain ever invokes it), so it has to happen here
   * in init() to happen at all. */
  if (d->bus->ops->write_cmd(d->bus, 0x01)) return SGFX_ERR_EIO;  /* SWRESET */
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 120);

  int rc = run_table(d, ST7121_INIT, ST7121_INIT_N);
  if (rc) return rc;
  rc = sgfx_hal_dsi_start_video(d->bus);
  if (rc) return rc;

  /* DISPON sent a second time, after DPI video start -- m5stack_tab5.c
   * calls esp_lcd_panel_disp_on_off(disp_panel, true) strictly after
   * esp_lcd_panel_init() returns, not just relying on the vendor array's
   * own (pre-video) 0x29. See ili9881c.c's init() for the same fix with
   * the source citation. */
  if (d->bus->ops->write_cmd(d->bus, 0x29)) return SGFX_ERR_EIO;
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 20);
  return SGFX_OK;
}

static void st7121_reset(sgfx_device_t* d){
  /* No-op on Tab5 -- see ili9881c.c's reset() comment, same rationale
   * (pin_rst == -1, LCD_RST is an I2C GPIO-expander bit outside this bus). */
  if (!d->bus->ops->gpio_set) return;
  d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, false);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
  d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, true);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
}

static void* st7121_get_fb_ptr(sgfx_device_t* d, size_t* out_stride_bytes){
  return sgfx_hal_dsi_get_fb(d->bus, out_stride_bytes);
}

static int st7121_flush_surface(sgfx_device_t* d, int x, int y, int w, int h){
  return sgfx_hal_dsi_flush(d->bus, x, y, w, h);
}

static int st7121_present(sgfx_device_t* d){
  return d->drv->flush_surface(d, 0, 0, d->caps.width, d->caps.height);
}

static int st7121_set_rotation(sgfx_device_t* d, uint8_t rot){
  /* See ili9881c.c's set_rotation() comment: no verified-safe mid-stream
   * rotation path for this IC in video mode. Known limitation. */
  (void)d; (void)rot;
  return SGFX_ERR_NOSUP;
}

static int st7121_power(sgfx_device_t* d, bool on){
  /* Documented no-op -- backlight (GPIO22 PWM/LEDC) is not reachable from
   * this bus. See ili9881c.c's power() comment for full rationale. */
  (void)d; (void)on;
  return SGFX_OK;
}

const sgfx_driver_ops_t sgfx_st7121_ops = {
  .init          = st7121_init,
  .reset         = st7121_reset,
  .set_rotation  = st7121_set_rotation,
  .set_window    = NULL,
  .write_pixels  = NULL,
  .fill_rect     = NULL,
  .power         = st7121_power,
  .invert        = NULL,
  .brightness    = NULL,   /* no PWM control reachable here -- known gap,
                             * belongs at the application layer. */
  .present       = st7121_present,
  .get_fb_ptr    = st7121_get_fb_ptr,
  .flush_surface = st7121_flush_surface,
};

const sgfx_caps_t sgfx_st7121_caps_default = {
  .width      = ST7121_W,
  .height     = ST7121_H,
  .native_fmt = SGFX_FMT_RGB565,
  .bpp        = 16,
  .caps       = SGFX_CAP_DSI,
};

#endif /* SGFX_DRV_ST7121 */
