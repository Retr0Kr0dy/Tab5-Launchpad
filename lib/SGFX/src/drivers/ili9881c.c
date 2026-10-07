#ifdef SGFX_DRV_ILI9881C

#include "sgfx.h"
#include "sgfx_hal.h"
#include <stdint.h>
#include <stddef.h>

/*
 * ili9881c.c — M5Stack Tab5 ILI9881C MIPI-DSI panel driver.
 *
 * Video-mode DSI panel: the DPI peripheral scans out a RAM-resident
 * framebuffer continuously, so this driver has nothing resembling
 * set_window/write_pixels. Its only job is:
 *   1. init()   — push the vendor DCS init sequence over the low-speed
 *                 command channel (bus->ops->write_cmd/write_data), then
 *                 start the DPI video stream.
 *   2. get_fb_ptr/flush_surface — thin passthroughs to the DSI HAL's
 *                 direct-framebuffer accessors (see sgfx_hal.h).
 *
 * DCS init sequence transcribed from M5Stack's own official Tab5 reference
 * firmware (read-only reference, never linked into this repo):
 *   M5Tab5-UserDemo/platforms/tab5/components/m5stack_tab5/include/bsp/ili9881_init_data.c
 *   (array tab5_lcd_ili9881c_specific_init_code_default)
 * Only the numeric {cmd, data, delay} values were transcribed; the table
 * struct and the loop that walks it below are this driver's own, not a
 * copy of the reference's C structure/logic.
 *
 * One entry present in the reference source is commented out there
 * (`{0x11, ..., 0}` — SLPOUT) and is therefore *not* transcribed here either:
 * we send exactly the sequence M5Stack ships, nothing guessed on top of it.
 *
 * DPI timing / DSI lane parameters below come from the actual Tab5 board
 * bring-up code (m5stack_tab5.c: bsp_display_new_with_handles(), the
 * ili9881c branch), which is more authoritative than the generic per-panel
 * component headers (those carry template/default values not actually used
 * on this board). Notably the *porch* values there (140/40/40 H, 20/4/20 V) but the lane
 * bitrate/DPI clock differ from the rough figures quoted elsewhere:
 *   BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS == 730 (display.h; the same macro's
 *     comment mentions 900 as a prior/alternate value, but 730 is what the
 *     shipped bring-up code actually acquires the DSI bus with)
 *   dpi_clock_freq_mhz == 60 (m5stack_tab5.c; a commented-out "// 80" sits
 *     next to it as an alternate the shipped code does *not* use)
 * This driver uses the values the shipped bring-up code actually runs with
 * (730 Mbps / 60 MHz), not the commented-out alternates.
 */

#define ILI9881C_W 720
#define ILI9881C_H 1280

/* ---- Own init-table idiom (not the reference's struct/loop) ---- */
typedef struct {
  uint8_t        cmd;
  uint8_t        len;    /* number of parameter bytes, 0 if none */
  const uint8_t* data;   /* NULL when len == 0 */
  uint16_t       delay_ms;
} dsi_init_cmd_t;

static const dsi_init_cmd_t ILI9881C_INIT[] = {
  /**** CMD_Page 1 ****/
  {0xFF, 3, (const uint8_t[]){0x98,0x81,0x01}, 0},
  {0xB7, 1, (const uint8_t[]){0x03}, 0},  /* set 2 lane */

  /**** CMD_Page 3 ****/
  {0xFF, 3, (const uint8_t[]){0x98,0x81,0x03}, 0},
  {0x01, 1, (const uint8_t[]){0x00}, 0}, {0x02, 1, (const uint8_t[]){0x00}, 0},
  {0x03, 1, (const uint8_t[]){0x73}, 0}, {0x04, 1, (const uint8_t[]){0x00}, 0},
  {0x05, 1, (const uint8_t[]){0x00}, 0}, {0x06, 1, (const uint8_t[]){0x08}, 0},
  {0x07, 1, (const uint8_t[]){0x00}, 0}, {0x08, 1, (const uint8_t[]){0x00}, 0},
  {0x09, 1, (const uint8_t[]){0x1B}, 0}, {0x0a, 1, (const uint8_t[]){0x01}, 0},
  {0x0b, 1, (const uint8_t[]){0x01}, 0}, {0x0c, 1, (const uint8_t[]){0x0D}, 0},
  {0x0d, 1, (const uint8_t[]){0x01}, 0}, {0x0e, 1, (const uint8_t[]){0x01}, 0},
  {0x0f, 1, (const uint8_t[]){0x26}, 0}, {0x10, 1, (const uint8_t[]){0x26}, 0},
  {0x11, 1, (const uint8_t[]){0x00}, 0}, {0x12, 1, (const uint8_t[]){0x00}, 0},
  {0x13, 1, (const uint8_t[]){0x02}, 0}, {0x14, 1, (const uint8_t[]){0x00}, 0},
  {0x15, 1, (const uint8_t[]){0x00}, 0}, {0x16, 1, (const uint8_t[]){0x00}, 0},
  {0x17, 1, (const uint8_t[]){0x00}, 0}, {0x18, 1, (const uint8_t[]){0x00}, 0},
  {0x19, 1, (const uint8_t[]){0x00}, 0}, {0x1a, 1, (const uint8_t[]){0x00}, 0},
  {0x1b, 1, (const uint8_t[]){0x00}, 0}, {0x1c, 1, (const uint8_t[]){0x00}, 0},
  {0x1d, 1, (const uint8_t[]){0x00}, 0}, {0x1e, 1, (const uint8_t[]){0x40}, 0},
  {0x1f, 1, (const uint8_t[]){0x00}, 0}, {0x20, 1, (const uint8_t[]){0x06}, 0},
  {0x21, 1, (const uint8_t[]){0x01}, 0}, {0x22, 1, (const uint8_t[]){0x00}, 0},
  {0x23, 1, (const uint8_t[]){0x00}, 0}, {0x24, 1, (const uint8_t[]){0x00}, 0},
  {0x25, 1, (const uint8_t[]){0x00}, 0}, {0x26, 1, (const uint8_t[]){0x00}, 0},
  {0x27, 1, (const uint8_t[]){0x00}, 0}, {0x28, 1, (const uint8_t[]){0x33}, 0},
  {0x29, 1, (const uint8_t[]){0x03}, 0}, {0x2a, 1, (const uint8_t[]){0x00}, 0},
  {0x2b, 1, (const uint8_t[]){0x00}, 0}, {0x2c, 1, (const uint8_t[]){0x00}, 0},
  {0x2d, 1, (const uint8_t[]){0x00}, 0}, {0x2e, 1, (const uint8_t[]){0x00}, 0},
  {0x2f, 1, (const uint8_t[]){0x00}, 0}, {0x30, 1, (const uint8_t[]){0x00}, 0},
  {0x31, 1, (const uint8_t[]){0x00}, 0}, {0x32, 1, (const uint8_t[]){0x00}, 0},
  {0x33, 1, (const uint8_t[]){0x00}, 0}, {0x34, 1, (const uint8_t[]){0x00}, 0},
  {0x35, 1, (const uint8_t[]){0x00}, 0}, {0x36, 1, (const uint8_t[]){0x00}, 0},
  {0x37, 1, (const uint8_t[]){0x00}, 0}, {0x38, 1, (const uint8_t[]){0x00}, 0},
  {0x39, 1, (const uint8_t[]){0x00}, 0}, {0x3a, 1, (const uint8_t[]){0x00}, 0},
  {0x3b, 1, (const uint8_t[]){0x00}, 0}, {0x3c, 1, (const uint8_t[]){0x00}, 0},
  {0x3d, 1, (const uint8_t[]){0x00}, 0}, {0x3e, 1, (const uint8_t[]){0x00}, 0},
  {0x3f, 1, (const uint8_t[]){0x00}, 0}, {0x40, 1, (const uint8_t[]){0x00}, 0},
  {0x41, 1, (const uint8_t[]){0x00}, 0}, {0x42, 1, (const uint8_t[]){0x00}, 0},
  {0x43, 1, (const uint8_t[]){0x00}, 0}, {0x44, 1, (const uint8_t[]){0x00}, 0},

  {0x50, 1, (const uint8_t[]){0x01}, 0}, {0x51, 1, (const uint8_t[]){0x23}, 0},
  {0x52, 1, (const uint8_t[]){0x45}, 0}, {0x53, 1, (const uint8_t[]){0x67}, 0},
  {0x54, 1, (const uint8_t[]){0x89}, 0}, {0x55, 1, (const uint8_t[]){0xab}, 0},
  {0x56, 1, (const uint8_t[]){0x01}, 0}, {0x57, 1, (const uint8_t[]){0x23}, 0},
  {0x58, 1, (const uint8_t[]){0x45}, 0}, {0x59, 1, (const uint8_t[]){0x67}, 0},
  {0x5a, 1, (const uint8_t[]){0x89}, 0}, {0x5b, 1, (const uint8_t[]){0xab}, 0},
  {0x5c, 1, (const uint8_t[]){0xcd}, 0}, {0x5d, 1, (const uint8_t[]){0xef}, 0},

  {0x5e, 1, (const uint8_t[]){0x11}, 0}, {0x5f, 1, (const uint8_t[]){0x02}, 0},
  {0x60, 1, (const uint8_t[]){0x00}, 0}, {0x61, 1, (const uint8_t[]){0x07}, 0},
  {0x62, 1, (const uint8_t[]){0x06}, 0}, {0x63, 1, (const uint8_t[]){0x0E}, 0},
  {0x64, 1, (const uint8_t[]){0x0F}, 0}, {0x65, 1, (const uint8_t[]){0x0C}, 0},
  {0x66, 1, (const uint8_t[]){0x0D}, 0}, {0x67, 1, (const uint8_t[]){0x02}, 0},
  {0x68, 1, (const uint8_t[]){0x02}, 0}, {0x69, 1, (const uint8_t[]){0x02}, 0},
  {0x6a, 1, (const uint8_t[]){0x02}, 0}, {0x6b, 1, (const uint8_t[]){0x02}, 0},
  {0x6c, 1, (const uint8_t[]){0x02}, 0}, {0x6d, 1, (const uint8_t[]){0x02}, 0},
  {0x6e, 1, (const uint8_t[]){0x02}, 0}, {0x6f, 1, (const uint8_t[]){0x02}, 0},
  {0x70, 1, (const uint8_t[]){0x02}, 0}, {0x71, 1, (const uint8_t[]){0x02}, 0},
  {0x72, 1, (const uint8_t[]){0x02}, 0}, {0x73, 1, (const uint8_t[]){0x05}, 0},
  {0x74, 1, (const uint8_t[]){0x01}, 0}, {0x75, 1, (const uint8_t[]){0x02}, 0},
  {0x76, 1, (const uint8_t[]){0x00}, 0}, {0x77, 1, (const uint8_t[]){0x07}, 0},
  {0x78, 1, (const uint8_t[]){0x06}, 0}, {0x79, 1, (const uint8_t[]){0x0E}, 0},
  {0x7a, 1, (const uint8_t[]){0x0F}, 0}, {0x7b, 1, (const uint8_t[]){0x0C}, 0},
  {0x7c, 1, (const uint8_t[]){0x0D}, 0}, {0x7d, 1, (const uint8_t[]){0x02}, 0},
  {0x7e, 1, (const uint8_t[]){0x02}, 0}, {0x7f, 1, (const uint8_t[]){0x02}, 0},
  {0x80, 1, (const uint8_t[]){0x02}, 0}, {0x81, 1, (const uint8_t[]){0x02}, 0},
  {0x82, 1, (const uint8_t[]){0x02}, 0}, {0x83, 1, (const uint8_t[]){0x02}, 0},
  {0x84, 1, (const uint8_t[]){0x02}, 0}, {0x85, 1, (const uint8_t[]){0x02}, 0},
  {0x86, 1, (const uint8_t[]){0x02}, 0}, {0x87, 1, (const uint8_t[]){0x02}, 0},
  {0x88, 1, (const uint8_t[]){0x02}, 0}, {0x89, 1, (const uint8_t[]){0x05}, 0},
  {0x8A, 1, (const uint8_t[]){0x01}, 0},

  /**** CMD_Page 4 ****/
  {0xFF, 3, (const uint8_t[]){0x98,0x81,0x04}, 0},
  {0x38, 1, (const uint8_t[]){0x01}, 0}, {0x39, 1, (const uint8_t[]){0x00}, 0},
  {0x6C, 1, (const uint8_t[]){0x15}, 0}, {0x6E, 1, (const uint8_t[]){0x1A}, 0},
  {0x6F, 1, (const uint8_t[]){0x25}, 0}, {0x3A, 1, (const uint8_t[]){0xA4}, 0},
  {0x8D, 1, (const uint8_t[]){0x20}, 0}, {0x87, 1, (const uint8_t[]){0xBA}, 0},
  {0x3B, 1, (const uint8_t[]){0x98}, 0},

  /**** CMD_Page 1 ****/
  {0xFF, 3, (const uint8_t[]){0x98,0x81,0x01}, 0},
  {0x22, 1, (const uint8_t[]){0x0A}, 0}, {0x31, 1, (const uint8_t[]){0x00}, 0},
  {0x50, 1, (const uint8_t[]){0x6B}, 0}, {0x51, 1, (const uint8_t[]){0x66}, 0},
  {0x53, 1, (const uint8_t[]){0x73}, 0}, {0x55, 1, (const uint8_t[]){0x8B}, 0},
  {0x60, 1, (const uint8_t[]){0x1B}, 0}, {0x61, 1, (const uint8_t[]){0x01}, 0},
  {0x62, 1, (const uint8_t[]){0x0C}, 0}, {0x63, 1, (const uint8_t[]){0x00}, 0},

  /* Gamma P */
  {0xA0, 1, (const uint8_t[]){0x00}, 0}, {0xA1, 1, (const uint8_t[]){0x15}, 0},
  {0xA2, 1, (const uint8_t[]){0x1F}, 0}, {0xA3, 1, (const uint8_t[]){0x13}, 0},
  {0xA4, 1, (const uint8_t[]){0x11}, 0}, {0xA5, 1, (const uint8_t[]){0x21}, 0},
  {0xA6, 1, (const uint8_t[]){0x17}, 0}, {0xA7, 1, (const uint8_t[]){0x1B}, 0},
  {0xA8, 1, (const uint8_t[]){0x6B}, 0}, {0xA9, 1, (const uint8_t[]){0x1E}, 0},
  {0xAA, 1, (const uint8_t[]){0x2B}, 0}, {0xAB, 1, (const uint8_t[]){0x5D}, 0},
  {0xAC, 1, (const uint8_t[]){0x19}, 0}, {0xAD, 1, (const uint8_t[]){0x14}, 0},
  {0xAE, 1, (const uint8_t[]){0x4B}, 0}, {0xAF, 1, (const uint8_t[]){0x1D}, 0},
  {0xB0, 1, (const uint8_t[]){0x27}, 0}, {0xB1, 1, (const uint8_t[]){0x49}, 0},
  {0xB2, 1, (const uint8_t[]){0x5D}, 0}, {0xB3, 1, (const uint8_t[]){0x39}, 0},

  /* Gamma N */
  {0xC0, 1, (const uint8_t[]){0x00}, 0}, {0xC1, 1, (const uint8_t[]){0x01}, 0},
  {0xC2, 1, (const uint8_t[]){0x0C}, 0}, {0xC3, 1, (const uint8_t[]){0x11}, 0},
  {0xC4, 1, (const uint8_t[]){0x15}, 0}, {0xC5, 1, (const uint8_t[]){0x28}, 0},
  {0xC6, 1, (const uint8_t[]){0x1B}, 0}, {0xC7, 1, (const uint8_t[]){0x1C}, 0},
  {0xC8, 1, (const uint8_t[]){0x62}, 0}, {0xC9, 1, (const uint8_t[]){0x1C}, 0},
  {0xCA, 1, (const uint8_t[]){0x29}, 0}, {0xCB, 1, (const uint8_t[]){0x60}, 0},
  {0xCC, 1, (const uint8_t[]){0x16}, 0}, {0xCD, 1, (const uint8_t[]){0x17}, 0},
  {0xCE, 1, (const uint8_t[]){0x4A}, 0}, {0xCF, 1, (const uint8_t[]){0x23}, 0},
  {0xD0, 1, (const uint8_t[]){0x24}, 0}, {0xD1, 1, (const uint8_t[]){0x4F}, 0},
  {0xD2, 1, (const uint8_t[]){0x5F}, 0}, {0xD3, 1, (const uint8_t[]){0x39}, 0},

  /**** CMD_Page 0 ****/
  {0xFF, 3, (const uint8_t[]){0x98,0x81,0x00}, 0},
  {0x35, 0, NULL, 0},
  {0xFE, 0, NULL, 0},
  {0x29, 0, NULL, 0},
};
#define ILI9881C_INIT_N (sizeof(ILI9881C_INIT)/sizeof(ILI9881C_INIT[0]))

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

/*
 * Preamble sent unconditionally before ILI9881C_INIT, every time.
 *
 * ILI9881C_INIT (above) is M5Stack's vendor-specific init table, transcribed
 * byte-for-byte -- but it is not a complete, standalone init sequence.
 * Espressif's official `esp_lcd_ili9881c` component sends this exact
 * sequence unconditionally in panel_ili9881c_init(), BEFORE it ever touches
 * the caller-supplied vendor_config.init_cmds array:
 *
 *   1. CMD_Page 0 select
 *   2. SLPOUT (0x11, no params), then vTaskDelay(120ms)   -- DCS-spec settle
 *   3. MADCTL (0x36) = 0x00   -- for LCD_RGB_ELEMENT_ORDER_RGB (Tab5's config)
 *   4. COLMOD (0x3A) = 0x55   -- for bits_per_pixel=16 / RGB565 (Tab5's config)
 *
 * Without this preamble the panel stays in MIPI sleep with no COLMOD set:
 * DISPON (present at the end of the transcribed array) is accepted with no
 * error and the framebuffer holds the correct rendered image, but the
 * screen shows nothing.
 *
 * madctl/colmod values hardcoded for Tab5's fixed config (RGB order, RGB565)
 * rather than plumbed through sgfx_hal_cfg_dsi_t -- this driver has always
 * been Tab5-specific, not generic.
 */
static const dsi_init_cmd_t ILI9881C_PREAMBLE[] = {
  {0xFF, 3, (const uint8_t[]){0x98,0x81,0x00}, 0},  /* CMD_Page 0 */
  {0x11, 0, NULL, 120},                              /* SLPOUT, then settle 120ms */
  {0x36, 1, (const uint8_t[]){0x00}, 0},              /* MADCTL: RGB order */
  {0x3A, 1, (const uint8_t[]){0x55}, 0},              /* COLMOD: RGB565 */
};
#define ILI9881C_PREAMBLE_N (sizeof(ILI9881C_PREAMBLE)/sizeof(ILI9881C_PREAMBLE[0]))

static int ili9881c_init(sgfx_device_t* d){
  /* Bus is already constructed by sgfx_open_dsi()/sgfx_hal_make_dsi() before
   * init() runs (see src/sgfx_factory.c) -- we only use d->bus here, we do
   * not build it.
   *
   * SWRESET first, matching esp_lcd_ili9881c's own panel_ili9881c_reset():
   * with reset_gpio_num == -1 (Tab5's case, physical reset is via SIC's I2C
   * GPIO expander, not a direct pin this driver can see), that function's
   * else-branch sends LCD_CMD_SWRESET (0x01) + 20ms delay over DCS instead
   * of toggling a GPIO. Confirmed by fetching and reading the real component
   * -- sgfx_driver_ops_t.reset is never actually invoked by anything in this
   * codebase's present call chain (sgfx_init()/sgfx_open_dsi() do not call
   * d->drv->reset()), so this has to happen here in init() to happen at all,
   * matching how every other panel driver in this codebase is self-contained
   * rather than relying on an external reset() call. */
  if (d->bus->ops->write_cmd(d->bus, 0x01)) return SGFX_ERR_EIO;   /* SWRESET */
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 20);

  int rc = run_table(d, ILI9881C_PREAMBLE, ILI9881C_PREAMBLE_N);
  if (rc) return rc;
  rc = run_table(d, ILI9881C_INIT, ILI9881C_INIT_N);
  if (rc) return rc;
  rc = sgfx_hal_dsi_start_video(d->bus);
  if (rc) return rc;

  /* ROOT CAUSE FIX #2 (verified against the real m5stack_tab5.c source):
   * DISPON is sent TWICE in the working reference, not once. The vendor
   * array above already ends in its own {0x29,0,NULL,0} (matching
   * panel_ili9881c_init()'s own DISPON-bearing sequence), but
   * m5stack_tab5.c separately calls esp_lcd_panel_disp_on_off(disp_panel,
   * true) -- another DCS 0x29 -- strictly AFTER esp_lcd_panel_init()
   * returns, i.e. after the DPI video timing generator is already running.
   * We were only sending the first (pre-video) one. */
  if (d->bus->ops->write_cmd(d->bus, 0x29)) return SGFX_ERR_EIO;
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 20);
  return SGFX_OK;
}

static void ili9881c_reset(sgfx_device_t* d){
  /* Legitimate no-op on Tab5: pin_rst == -1 in this panel's sgfx_hal_cfg_dsi_t,
   * so espidf_dsi.c's gpio_set(SGFX_GPIO_RST) already does nothing. Reset is
   * pulsed externally via SIC's tab5_ioexp_init() before SGFX ever runs.
   * Kept as a real toggle (not just `return;`) so this driver stays reusable
   * on a hypothetical future board with a real DSI reset GPIO. */
  if (!d->bus->ops->gpio_set) return;
  d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, false);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
  d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, true);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
}

static void* ili9881c_get_fb_ptr(sgfx_device_t* d, size_t* out_stride_bytes){
  return sgfx_hal_dsi_get_fb(d->bus, out_stride_bytes);
}

static int ili9881c_flush_surface(sgfx_device_t* d, int x, int y, int w, int h){
  return sgfx_hal_dsi_flush(d->bus, x, y, w, h);
}

static int ili9881c_present(sgfx_device_t* d){
  return d->drv->flush_surface(d, 0, 0, d->caps.width, d->caps.height);
}

static int ili9881c_set_rotation(sgfx_device_t* d, uint8_t rot){
  /* Video-mode DSI panels don't rotate the way windowed SPI panels do: there
   * is no CASET/RASET-style row/column remap available mid-stream without
   * risking a garbled live scanout, and no MADCTL-equivalent for this IC is
   * confirmed safe to send after the DPI stream has started. Known
   * limitation for a later phase (a software-side framebuffer rotate would
   * be the safe fallback). */
  (void)d; (void)rot;
  return SGFX_ERR_NOSUP;
}

static int ili9881c_power(sgfx_device_t* d, bool on){
  /* Tab5's backlight is a separate PWM/LEDC channel on GPIO22, owned by
   * whoever drives the display lifecycle at the application layer -- not
   * reachable from this bus. Panel power for a DSI panel is really "is the
   * DPI stream running," which init()/sgfx_hal_dsi_start_video() already
   * handles, so this is a documented no-op rather than a fake backlight
   * control. */
  (void)d; (void)on;
  return SGFX_OK;
}

const sgfx_driver_ops_t sgfx_ili9881c_ops = {
  .init          = ili9881c_init,
  .reset         = ili9881c_reset,
  .set_rotation  = ili9881c_set_rotation,
  .set_window    = NULL,
  .write_pixels  = NULL,
  .fill_rect     = NULL,
  .power         = ili9881c_power,
  .invert        = NULL,
  .brightness    = NULL,   /* no PWM control reachable from this bus's boolean
                             * gpio_set; real brightness belongs wherever
                             * GPIO22's LEDC channel is owned (application
                             * layer), not here. Known gap, see report. */
  .present       = ili9881c_present,
  .get_fb_ptr    = ili9881c_get_fb_ptr,
  .flush_surface = ili9881c_flush_surface,
};

const sgfx_caps_t sgfx_ili9881c_caps_default = {
  .width      = ILI9881C_W,
  .height     = ILI9881C_H,
  .native_fmt = SGFX_FMT_RGB565,
  .bpp        = 16,
  .caps       = SGFX_CAP_DSI,
};

#endif /* SGFX_DRV_ILI9881C */
