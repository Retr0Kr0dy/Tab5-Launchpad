#ifdef SGFX_DRV_ST7123

#include "sgfx.h"
#include "sgfx_hal.h"
#include <stdint.h>
#include <stddef.h>

/*
 * st7123.c — M5Stack Tab5 ST7123 MIPI-DSI panel driver.
 *
 * Same video-mode-DSI shape as ili9881c.c in this directory: init() pushes
 * the vendor DCS sequence over the bus's write_cmd/write_data ops then starts
 * the DPI video stream; get_fb_ptr/flush_surface are thin passthroughs to the
 * DSI HAL. See ili9881c.c's file header for the general rationale, not
 * repeated here.
 *
 * DCS init sequence transcribed from M5Stack's own official Tab5 reference
 * firmware (read-only reference, never linked into this repo). Two source
 * copies of this table exist there:
 *   - the ST7123 component's own file-local default:
 *     M5Tab5-UserDemo/platforms/tab5/components/m5stack_tab5/esp_lcd_st7123.c
 *     (array vendor_specific_init_default)
 *   - the actual Tab5 board bring-up's override, passed in explicitly:
 *     M5Tab5-UserDemo/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c
 *     (array st7123_vendor_specific_init_default, used by
 *     bsp_display_new_with_handles_to_st7123())
 * They are identical except one byte: the MADCTL (0x36) parameter is 0x03 in
 * the component's generic default but 0x00 in the board bring-up's override.
 * Transcribed here from m5stack_tab5.c's version (0x36 = 0x00) since that is
 * what actually runs on real Tab5 hardware, not the component's generic
 * fallback.
 *
 * DPI timing / DSI lane parameters below come from the same board bring-up
 * function (bsp_display_new_with_handles_to_st7123() in m5stack_tab5.c),
 * which is more authoritative than the ST7123 component header's
 * ST7123_800_1280_PANEL_60HZ_DPI_CONFIG() macro (that macro describes an
 * 800x1280/60Hz/80MHz config that is never actually invoked for Tab5 --
 * a template for a different panel size, not this board). The porch values
 * the board bring-up code uses (H=40/2/40, V=8/2/220) but its lane bitrate/DPI clock are the
 * literal numbers the shipped code acquires the DSI bus with:
 *   lane_bit_rate_mbps == 965   (shared with ST7121, same DSI bus config)
 *   dpi_clock_freq_mhz == 70
 * not the component header's 80 MHz.
 */

#define ST7123_W 720
#define ST7123_H 1280

/* ---- Own init-table idiom (not the reference's struct/loop) ---- */
typedef struct {
  uint8_t        cmd;
  uint8_t        len;    /* number of parameter bytes, 0 if none */
  const uint8_t* data;   /* NULL when len == 0 */
  uint16_t       delay_ms;
} dsi_init_cmd_t;

static const dsi_init_cmd_t ST7123_INIT[] = {
  {0x60, 3, (const uint8_t[]){0x71,0x23,0xa2}, 0},
  {0x60, 3, (const uint8_t[]){0x71,0x23,0xa3}, 0},
  {0x60, 3, (const uint8_t[]){0x71,0x23,0xa4}, 0},
  {0xA4, 1, (const uint8_t[]){0x31}, 0},
  {0xD7, 6, (const uint8_t[]){0x10,0x0A,0x10,0x2A,0x80,0x80}, 0},
  {0x90, 7, (const uint8_t[]){0x71,0x23,0x5A,0x20,0x24,0x09,0x09}, 0},
  /* Reference source declares this command's data_bytes as 40 but only
   * lists 39 literal byte values (a mismatch in M5Stack's own array
   * initializer -- the other large tables below all have declared/actual
   * counts that agree). Transcribed here as len=39, the literal count
   * actually given, rather than fabricating an unknown 40th byte. */
  {0xA3, 39, (const uint8_t[]){
    0x80,0x01,0x88,0x30,0x05,0x00,0x00,0x00,0x00,0x00,0x46,0x00,0x00,
    0x1E,0x5C,0x1E,0x80,0x00,0x4F,0x05,0x00,0x00,0x00,0x00,0x00,0x46,
    0x00,0x00,0x1E,0x5C,0x1E,0x80,0x00,0x6F,0x58,0x00,0x00,0x00,0xFF}, 0},
  {0xA6, 55, (const uint8_t[]){
    0x03,0x00,0x24,0x55,0x36,0x00,0x39,0x00,0x6E,0x6E,0x91,0xFF,0x00,0x24,
    0x55,0x38,0x00,0x37,0x00,0x6E,0x6E,0x91,0xFF,0x00,0x24,0x11,0x00,0x00,
    0x00,0x00,0x6E,0x6E,0x91,0xFF,0x00,0xEC,0x11,0x00,0x03,0x00,0x03,0x6E,
    0x6E,0xFF,0xFF,0x00,0x08,0x80,0x08,0x80,0x06,0x00,0x00,0x00,0x00}, 0},
  {0xA7, 60, (const uint8_t[]){
    0x19,0x19,0x80,0x64,0x40,0x07,0x16,0x40,0x00,0x44,0x03,0x6E,0x6E,0x91,0xFF,
    0x08,0x80,0x64,0x40,0x25,0x34,0x40,0x00,0x02,0x01,0x6E,0x6E,0x91,0xFF,0x08,
    0x80,0x64,0x40,0x00,0x00,0x40,0x00,0x00,0x00,0x6E,0x6E,0x91,0xFF,0x08,0x80,
    0x64,0x40,0x00,0x00,0x00,0x00,0x20,0x00,0x6E,0x6E,0x84,0xFF,0x08,0x80,0x44}, 0},
  {0xAC, 44, (const uint8_t[]){
    0x03,0x19,0x19,0x18,0x18,0x06,0x13,0x13,0x11,0x11,0x08,0x08,0x0A,0x0A,0x1C,
    0x1C,0x07,0x07,0x00,0x00,0x02,0x02,0x01,0x19,0x19,0x18,0x18,0x06,0x12,0x12,
    0x10,0x10,0x09,0x09,0x0B,0x0B,0x1C,0x1C,0x07,0x07,0x03,0x03,0x01,0x01}, 0},
  {0xAD, 25, (const uint8_t[]){
    0xF0,0x00,0x46,0x00,0x03,0x50,0x50,0xFF,0xFF,0xF0,0x40,0x06,0x01,
    0x07,0x42,0x42,0xFF,0xFF,0x01,0x00,0x00,0xFF,0xFF,0xFF,0xFF}, 0},
  {0xAE, 7, (const uint8_t[]){0xFE,0x3F,0x3F,0xFE,0x3F,0x3F,0x00}, 0},
  {0xB2, 17, (const uint8_t[]){
    0x15,0x19,0x05,0x23,0x49,0xAF,0x03,0x2E,0x5C,0xD2,0xFF,0x10,0x20,0xFD,0x20,0xC0,0x00}, 0},
  {0xE8, 14, (const uint8_t[]){
    0x20,0x6F,0x04,0x97,0x97,0x3E,0x04,0xDC,0xDC,0x3E,0x06,0xFA,0x26,0x3E}, 0},
  {0x75, 2, (const uint8_t[]){0x03,0x04}, 0},
  {0xE7, 36, (const uint8_t[]){
    0x3B,0x00,0x00,0x7C,0xA1,0x8C,0x20,0x1A,0xF0,0xB1,0x50,0x00,
    0x50,0xB1,0x50,0xB1,0x50,0xD8,0x00,0x55,0x00,0xB1,0x00,0x45,
    0xC9,0x6A,0xFF,0x5A,0xD8,0x18,0x88,0x15,0xB1,0x01,0x01,0x77}, 0},
  {0xEA, 8, (const uint8_t[]){0x13,0x00,0x04,0x00,0x00,0x00,0x00,0x2C}, 0},
  {0xB0, 7, (const uint8_t[]){0x22,0x43,0x11,0x61,0x25,0x43,0x43}, 0},
  {0xB7, 4, (const uint8_t[]){0x00,0x00,0x73,0x73}, 0},
  {0xBF, 2, (const uint8_t[]){0xA6,0xAA}, 0},
  {0xA9, 10, (const uint8_t[]){0x00,0x00,0x73,0xFF,0x00,0x00,0x03,0x00,0x00,0x03}, 0},
  {0xC8, 37, (const uint8_t[]){
    0x00,0x00,0x10,0x1F,0x36,0x00,0x5D,0x04,0x9D,0x05,0x10,0xF2,0x06,
    0x60,0x03,0x11,0xAD,0x00,0xEF,0x01,0x22,0x2E,0x0E,0x74,0x08,0x32,
    0xDC,0x09,0x33,0x0F,0xF3,0x77,0x0D,0xB0,0xDC,0x03,0xFF}, 0},
  {0xC9, 37, (const uint8_t[]){
    0x00,0x00,0x10,0x1F,0x36,0x00,0x5D,0x04,0x9D,0x05,0x10,0xF2,0x06,
    0x60,0x03,0x11,0xAD,0x00,0xEF,0x01,0x22,0x2E,0x0E,0x74,0x08,0x32,
    0xDC,0x09,0x33,0x0F,0xF3,0x77,0x0D,0xB0,0xDC,0x03,0xFF}, 0},
  {0x36, 1, (const uint8_t[]){0x00}, 0},
  {0x11, 1, (const uint8_t[]){0x00}, 100},
  {0x29, 1, (const uint8_t[]){0x00}, 0},
  {0x35, 1, (const uint8_t[]){0x00}, 100},
};
#define ST7123_INIT_N (sizeof(ST7123_INIT)/sizeof(ST7123_INIT[0]))

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

static int st7123_init(sgfx_device_t* d){
  /* ROOT CAUSE FIX -- same as st7121.c / ili9881c.c, see those for the
   * source verification. With reset_gpio_num == -1, the vendor ST driver's
   * reset() falls back to a DCS software reset (SWRESET) + settle delay,
   * called BEFORE init(); this codebase's sgfx_init()/sgfx_open_dsi() never
   * calls d->drv->reset() at all, so it has to happen here to happen. */
  if (d->bus->ops->write_cmd(d->bus, 0x01)) return SGFX_ERR_EIO;  /* SWRESET */
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 120);

  int rc = run_table(d, ST7123_INIT, ST7123_INIT_N);
  if (rc) return rc;
  rc = sgfx_hal_dsi_start_video(d->bus);
  if (rc) return rc;

  /* DISPON sent a second time, after DPI video start -- see ili9881c.c's
   * init() for the source citation (m5stack_tab5.c calls
   * esp_lcd_panel_disp_on_off(disp_panel, true) strictly after
   * esp_lcd_panel_init() returns, not just relying on the vendor array's
   * own pre-video 0x29). */
  if (d->bus->ops->write_cmd(d->bus, 0x29)) return SGFX_ERR_EIO;
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 20);
  return SGFX_OK;
}

static void st7123_reset(sgfx_device_t* d){
  /* No-op on Tab5 -- see ili9881c.c's reset() comment, same rationale
   * (pin_rst == -1, LCD_RST is an I2C GPIO-expander bit outside this bus). */
  if (!d->bus->ops->gpio_set) return;
  d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, false);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
  d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, true);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
}

static void* st7123_get_fb_ptr(sgfx_device_t* d, size_t* out_stride_bytes){
  return sgfx_hal_dsi_get_fb(d->bus, out_stride_bytes);
}

static int st7123_flush_surface(sgfx_device_t* d, int x, int y, int w, int h){
  return sgfx_hal_dsi_flush(d->bus, x, y, w, h);
}

static int st7123_present(sgfx_device_t* d){
  return d->drv->flush_surface(d, 0, 0, d->caps.width, d->caps.height);
}

static int st7123_set_rotation(sgfx_device_t* d, uint8_t rot){
  /* See ili9881c.c's set_rotation() comment: no verified-safe mid-stream
   * rotation path for this IC in video mode. Known limitation. */
  (void)d; (void)rot;
  return SGFX_ERR_NOSUP;
}

static int st7123_power(sgfx_device_t* d, bool on){
  /* Documented no-op -- backlight (GPIO22 PWM/LEDC) is not reachable from
   * this bus. See ili9881c.c's power() comment for full rationale. */
  (void)d; (void)on;
  return SGFX_OK;
}

const sgfx_driver_ops_t sgfx_st7123_ops = {
  .init          = st7123_init,
  .reset         = st7123_reset,
  .set_rotation  = st7123_set_rotation,
  .set_window    = NULL,
  .write_pixels  = NULL,
  .fill_rect     = NULL,
  .power         = st7123_power,
  .invert        = NULL,
  .brightness    = NULL,   /* no PWM control reachable here -- known gap,
                             * belongs at the application layer. */
  .present       = st7123_present,
  .get_fb_ptr    = st7123_get_fb_ptr,
  .flush_surface = st7123_flush_surface,
};

const sgfx_caps_t sgfx_st7123_caps_default = {
  .width      = ST7123_W,
  .height     = ST7123_H,
  .native_fmt = SGFX_FMT_RGB565,
  .bpp        = 16,
  .caps       = SGFX_CAP_DSI,
};

#endif /* SGFX_DRV_ST7123 */
