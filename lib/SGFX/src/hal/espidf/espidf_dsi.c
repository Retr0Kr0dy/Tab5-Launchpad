/*
 * espidf_dsi.c — SGFX MIPI-DSI bus backend for ESP-IDF (ESP32-P4 class SoCs).
 *
 * Unlike the SPI/I2C backends, a DSI panel in video mode is *not* driven by
 * pushing every pixel over a command bus. The SoC's DSI bridge continuously
 * DMAs a RAM-resident framebuffer out to the panel; the CPU's job is to write
 * pixels into that buffer and tell the hardware which region changed.
 *
 * So this backend splits into two very different halves:
 *
 *   1. A low-speed DCS command channel, exposed through the ordinary
 *      sgfx_bus_ops_t write_cmd/write_data/delay_ms/gpio_set members. Panel
 *      drivers use it exactly like they use the SPI backend, to push their
 *      vendor init sequences.
 *
 *   2. A direct-framebuffer surface, which sgfx_bus_ops_t simply cannot
 *      express (it is a byte-stream / MIPI-DBI contract). It is exposed
 *      instead through three named entry points declared in sgfx_hal.h:
 *      sgfx_hal_dsi_start_video(), sgfx_hal_dsi_get_fb() and
 *      sgfx_hal_dsi_flush(). A DSI panel driver wires those into
 *      sgfx_driver_ops_t's init / get_fb_ptr / flush_surface, and leaves
 *      set_window/write_pixels NULL; sgfx_present_frame() then takes its
 *      direct-framebuffer path.
 *
 * Correspondingly, sgfx_bus_ops_t.write_pixels, .write_repeat and .read_data
 * are deliberately left NULL here: there is no row-streamed pixel path on this
 * transport at the *bus* layer. (Note this is the bus-level write_pixels, not
 * the driver-level one — two different members in two different structs.)
 *
 * Accepted platform-SDK dependency tier: ESP-IDF's own esp_lcd_mipi_dsi /
 * esp_lcd_panel_io / esp_lcd_panel_ops, same standing as driver/spi_master.h
 * in espidf_spi.c. No LVGL, no esp_lvgl_port, no C++.
 *
 * Verified against ESP-IDF v5.5.4 headers.
 */

#ifdef SGFX_HAL_ESPIDF

#include "soc/soc_caps.h"

#if defined(SOC_MIPI_DSI_SUPPORTED) && SOC_MIPI_DSI_SUPPORTED

#include "sgfx.h"
#include "sgfx_hal.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_cache.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifndef SGFX_IDF_DSI_BUS_ID
#  define SGFX_IDF_DSI_BUS_ID 0
#endif
#ifndef SGFX_IDF_DSI_VCHANNEL
#  define SGFX_IDF_DSI_VCHANNEL 0
#endif

typedef struct {
  esp_ldo_channel_handle_t  ldo;        /* DSI PHY power, NULL if unmanaged   */
  esp_lcd_dsi_bus_handle_t  dsi;        /* DSI host + D-PHY                   */
  esp_lcd_panel_io_handle_t io;         /* DBI (generic/LP) command channel   */
  esp_lcd_panel_handle_t    dpi;        /* DPI video-mode panel               */

  uint8_t* fb;                          /* scanout framebuffer (PSRAM)        */
  size_t   fb_stride;                   /* bytes per row                      */
  size_t   fb_bytes;                    /* total framebuffer size             */

  /* Real double-buffering, num_fbs==2 only (see sgfx_hal.h). fb/fb1 are the
   * two driver-owned buffers as returned by esp_lcd_dpi_panel_get_frame_buffer()
   * once, at creation time -- they never move; `active_fb` tracks which of
   * the two is currently live (being scanned out), updated only by
   * sgfx_hal_dsi_flip(). fb1/refresh_sem are NULL/unused when num_fbs<2. */
  uint8_t* fb1;
  uint8_t* active_fb;
  uint8_t  num_fbs;
  SemaphoreHandle_t refresh_sem;

  uint16_t width, height;
  uint8_t  bytes_pp;
  int      pin_rst;

  bool     video_started;

  /* write_cmd/write_data bridging state, see dsi_write_cmd() below. */
  bool     cmd_pending;
  uint8_t  pending_cmd;
} idf_dsi_ctx_t;

/* Forward decl so the ops table can be used for the "is this our bus" check. */
static const sgfx_bus_ops_t IDF_DSI_OPS;

static idf_dsi_ctx_t* ctx_of(sgfx_bus_t* b){
  if (!b || b->ops != &IDF_DSI_OPS) return NULL;
  return (idf_dsi_ctx_t*)b->user;
}

/* ---------------------------------------------------------------- commands */

static int dsi_tx(idf_dsi_ctx_t* c, uint8_t cmd, const void* param, size_t len){
  esp_err_t e = esp_lcd_panel_io_tx_param(c->io, (int)cmd, param, len);
  return (e == ESP_OK) ? SGFX_OK : SGFX_ERR_EIO;
}

/* Emit a stashed command that turned out to have no parameters. */
static int dsi_flush_pending(idf_dsi_ctx_t* c){
  if (!c || !c->cmd_pending) return SGFX_OK;
  c->cmd_pending = false;
  return dsi_tx(c, c->pending_cmd, NULL, 0);
}

/*
 * Command/data bridging — a real semantic translation, not a 1:1 mapping.
 *
 * The sgfx_bus_ops_t contract is MIPI-DBI shaped: write_cmd(cmd) puts one
 * command byte on the wire (DC low), a following write_data(buf,len) puts its
 * parameters on the wire (DC high) — two separate bus transactions.
 *
 * DSI's low-speed command mode has no DC line and no notion of a bare command
 * byte followed by loose data: one DCS packet carries the command byte *and*
 * all of its parameters, and esp_lcd_panel_io_tx_param() takes both at once.
 *
 * So write_cmd() only *stashes* the command byte, and the packet is actually
 * transmitted by whichever of these happens next:
 *   - write_data()  -> command + those parameters, as one DCS packet
 *   - write_cmd()   -> previous command flushed as a zero-parameter packet
 *   - delay_ms()    -> flushed first, so the delay really is *after* the
 *                      command (drivers rely on that ordering)
 *   - end()         -> flushed, closing out a transaction
 *   - sgfx_hal_dsi_start_video() -> flushed, so a trailing DISPON/SLPOUT is
 *                      on the wire before video starts.
 * Together those cover every way an SGFX panel driver can end a sequence.
 */
static int dsi_write_cmd(sgfx_bus_t* b, uint8_t cmd){
  idf_dsi_ctx_t* c = ctx_of(b);
  if (!c) return SGFX_ERR_INVAL;
  int rc = dsi_flush_pending(c);
  c->pending_cmd = cmd;
  c->cmd_pending = true;
  return rc;
}

static int dsi_write_data(sgfx_bus_t* b, const void* buf, size_t len){
  idf_dsi_ctx_t* c = ctx_of(b);
  if (!c) return SGFX_ERR_INVAL;
  if (!c->cmd_pending) return SGFX_ERR_INVAL;  /* parameters with no command */
  c->cmd_pending = false;
  return dsi_tx(c, c->pending_cmd, buf, len);
}

static int dsi_begin(sgfx_bus_t* b){
  /* DSI has no per-transaction bus claim / chip select. */
  (void)b;
  return SGFX_OK;
}

static void dsi_end(sgfx_bus_t* b){
  (void)dsi_flush_pending(ctx_of(b));
}

static void dsi_delay(sgfx_bus_t* b, uint32_t ms){
  (void)dsi_flush_pending(ctx_of(b));
  vTaskDelay(pdMS_TO_TICKS(ms));
}

/*
 * Side-band GPIO control.
 *
 * SGFX_GPIO_DC is meaningless on DSI (no data/command line) and is ignored.
 * SGFX_GPIO_BL is not owned by this backend — sgfx_hal_cfg_dsi_t carries no
 * backlight pin, because on the boards we target the backlight is a PWM/LEDC
 * channel driven by the board layer rather than a plain output.
 *
 * SGFX_GPIO_RST drives cfg->pin_rst if one was given. On M5Stack Tab5,
 * cfg->pin_rst is -1: the panel's reset line (LCD_RST, shared with TP_RST)
 * hangs off a PI4IOE5V6408 I2C GPIO expander, which this HAL has no access to
 * and must not grow a dependency on. Whoever calls sgfx_hal_make_dsi() /
 * sgfx_init() for Tab5 is responsible for pulsing panel reset through SIC's
 * Tab5 ioexpander module *before* calling in here; this function is then a
 * deliberate no-op and panel drivers' reset pulses harmlessly do nothing.
 */
static void dsi_gpio_set(sgfx_bus_t* b, int pin_id, bool level){
  idf_dsi_ctx_t* c = ctx_of(b);
  if (!c) return;
  int pin = -1;
  switch (pin_id){
    case SGFX_GPIO_RST: pin = c->pin_rst; break;
    case SGFX_GPIO_DC:  /* fallthrough */
    case SGFX_GPIO_BL:  /* fallthrough */
    default: break;
  }
  if (pin >= 0) gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

static const sgfx_bus_ops_t IDF_DSI_OPS = {
  .begin        = dsi_begin,
  .end          = dsi_end,
  .write_cmd    = dsi_write_cmd,
  .write_data   = dsi_write_data,
  .write_repeat = NULL,   /* no row-streamed pixel path on this transport */
  .write_pixels = NULL,   /* ditto — drivers use sgfx_hal_dsi_get_fb()    */
  .read_data    = NULL,   /* DCS reads need a command byte; not expressible here */
  .delay_ms     = dsi_delay,
  .gpio_set     = dsi_gpio_set
};

/* ------------------------------------------------------------ framebuffer */

int sgfx_hal_dsi_start_video(sgfx_bus_t* bus){
  idf_dsi_ctx_t* c = ctx_of(bus);
  if (!c) return SGFX_ERR_INVAL;
  int rc = dsi_flush_pending(c);
  if (rc) return rc;
  if (c->video_started) return SGFX_OK;
  /* esp_lcd_panel_init() on a DPI panel starts the continuous refresh. The DPI
   * panel implements only del/init/draw_bitmap, so esp_lcd_panel_disp_on_off()
   * is intentionally not called: "display on" is DCS 0x29, which belongs to the
   * panel driver's init sequence and goes out over the DBI channel above. */
  if (esp_lcd_panel_init(c->dpi) != ESP_OK) return SGFX_ERR_EIO;
  c->video_started = true;
  return SGFX_OK;
}

void* sgfx_hal_dsi_get_fb(sgfx_bus_t* bus, size_t* out_stride_bytes){
  idf_dsi_ctx_t* c = ctx_of(bus);
  if (!c) return NULL;
  if (out_stride_bytes) *out_stride_bytes = c->fb_stride;
  return c->fb;
}

/*
 * DIAGNOSTIC ONLY: reads back the ILI9881C's Page-1 ID registers
 * (0x00/0x01/0x02), the same esp_lcd_panel_io_rx_param() call the real
 * esp_lcd_ili9881c component makes at the top of its own init(). This bus
 * has no general DSI-level readback capability (bus->ops->read_data is NULL
 * by design, see the file header), so this is the only way to tell whether
 * the low-speed command channel is actually being understood by the panel,
 * versus merely "not erroring out" at the ESP-IDF transport level. Call
 * this directly (bypassing the sgfx_bus_ops_t write_cmd/write_data
 * bridging) right after sgfx_hal_make_dsi() succeeds, before running any
 * panel driver's DCS init table.
 */
int sgfx_hal_dsi_debug_read_ili9881c_id(sgfx_bus_t* bus, uint8_t* id1, uint8_t* id2, uint8_t* id3){
  idf_dsi_ctx_t* c = ctx_of(bus);
  if (!c || !id1 || !id2 || !id3) return SGFX_ERR_INVAL;
  *id1 = *id2 = *id3 = 0xEE; /* sentinel so a partial failure is visible */

  static const uint8_t page1[3] = {0x98, 0x81, 0x01};
  if (esp_lcd_panel_io_tx_param(c->io, 0xFF, page1, 3) != ESP_OK) return SGFX_ERR_EIO;

  esp_err_t e1 = esp_lcd_panel_io_rx_param(c->io, 0x00, id1, 1);
  esp_err_t e2 = esp_lcd_panel_io_rx_param(c->io, 0x01, id2, 1);
  esp_err_t e3 = esp_lcd_panel_io_rx_param(c->io, 0x02, id3, 1);

  return (e1 == ESP_OK && e2 == ESP_OK && e3 == ESP_OK) ? SGFX_OK : SGFX_ERR_EIO;
}

int sgfx_hal_dsi_flush(sgfx_bus_t* bus, int x, int y, int w, int h){
  idf_dsi_ctx_t* c = ctx_of(bus);
  if (!c || !c->fb) return SGFX_ERR_INVAL;
  if (w <= 0 || h <= 0) return SGFX_OK;

  /* The CPU has just written pixels into a cached, DMA-readable PSRAM buffer
   * and the DSI bridge is about to read them, so dirty cache lines covering
   * the rect must be written *back* to RAM: cache-to-memory (C2M) direction.
   * (M2C would be the opposite case — hardware wrote, CPU about to read.)
   *
   * Cache lines straddle row boundaries and the rect's rows are not
   * contiguous, so like ESP-IDF's own dpi_panel_draw_bitmap() we sync whole
   * rows and let the UNALIGNED flag cope with the ends. x/w therefore do not
   * narrow the synced range.
   */
  (void)x; (void)w;
  if (y < 0){ h += y; y = 0; }
  if (y >= (int)c->height) return SGFX_OK;
  if (y + h > (int)c->height) h = (int)c->height - y;
  if (h <= 0) return SGFX_OK;

  uint8_t* start = c->fb + (size_t)y * c->fb_stride;
  size_t   size  = (size_t)h * c->fb_stride;
  esp_err_t e = esp_cache_msync(start, size,
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  return (e == ESP_OK) ? SGFX_OK : SGFX_ERR_EIO;
}

/* ------------------------------------------------------- double-buffering */

/* ESP-IDF's own general-callback signature returns "was a higher-priority
 * task woken" for the ISR-yield contract; refresh_sem is only ever given
 * from here so there's nothing else that could need the yield, but the
 * portYIELD_FROM_ISR convention still requires reporting it accurately. */
static bool dsi_on_refresh_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t* edata, void* user_ctx){
  (void)panel; (void)edata;
  idf_dsi_ctx_t* c = (idf_dsi_ctx_t*)user_ctx;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(c->refresh_sem, &woken);
  return woken == pdTRUE;
}

void* sgfx_hal_dsi_get_back_fb(sgfx_bus_t* bus, size_t* out_stride_bytes){
  idf_dsi_ctx_t* c = ctx_of(bus);
  if (!c) return NULL;
  if (out_stride_bytes) *out_stride_bytes = c->fb_stride;
  if (c->num_fbs < 2) return c->fb; /* single-buffer: degrade to the one buffer */
  return (c->active_fb == c->fb) ? c->fb1 : c->fb;
}

static int dsi_flip_impl(sgfx_bus_t* bus, uint32_t timeout_ms, bool cpu_dirty){
  idf_dsi_ctx_t* c = ctx_of(bus);
  if (!c) return SGFX_ERR_INVAL;
  if (c->num_fbs < 2) return SGFX_OK; /* single-buffer: no-op, see sgfx_hal.h */

  uint8_t* back = (c->active_fb == c->fb) ? c->fb1 : c->fb;

  /* Normal callers wrote the back buffer from the CPU, so dirty cache lines
   * must be written back before DSI scans it. The PPA video fast path is
   * different: PPA/2D-DMA produced the pixels in PSRAM and the PPA driver
   * already performed the required cache maintenance. Re-walking the entire
   * 720x1280 RGB565 buffer here is pure bandwidth/latency in that case. */
  if (cpu_dirty) {
    esp_err_t sync_e = esp_cache_msync(back, c->fb_bytes,
                                       ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (sync_e != ESP_OK) return SGFX_ERR_EIO;
  }

  /* If the caller wants ownership safety, discard any refresh token left by
   * continuous DPI scanning BEFORE requesting this swap. Then the token we
   * wait for below necessarily belongs to a refresh occurring after this
   * draw_bitmap request, so the old front buffer is safe to recycle. */
  if (timeout_ms != 0) {
    while (xSemaphoreTake(c->refresh_sem, 0) == pdTRUE) {}
  }
  if (esp_lcd_panel_draw_bitmap(c->dpi, 0, 0, c->width, c->height, back) != ESP_OK) {
    return SGFX_ERR_EIO;
  }
  c->active_fb = back;
  if (timeout_ms == 0) return SGFX_OK;
  TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
  return (xSemaphoreTake(c->refresh_sem, ticks) == pdTRUE) ? SGFX_OK : SGFX_ERR_EIO;
}

int sgfx_hal_dsi_flip(sgfx_bus_t* bus, uint32_t timeout_ms){
  return dsi_flip_impl(bus, timeout_ms, true);
}

int sgfx_hal_dsi_flip_hw_written(sgfx_bus_t* bus, uint32_t timeout_ms){
  return dsi_flip_impl(bus, timeout_ms, false);
}

int sgfx_hal_dsi_flip_to_front(sgfx_bus_t* bus, uint32_t timeout_ms){
  idf_dsi_ctx_t* c = ctx_of(bus);
  if (!c) return SGFX_ERR_INVAL;
  if (c->num_fbs < 2) return SGFX_OK;
  /* Buffer 0 is what every normal SGFX drawing call (sgfx_present() et al,
   * via sgfx_hal_dsi_get_fb()) always targets -- if it's not currently the
   * live/scanned-out buffer, whatever drew there (e.g. the touch menu,
   * about to become visible again after a video exits) would silently not
   * show up until something flips back. Called once when a temporary
   * back-buffer user (the video player) is done. */
  if (c->active_fb == c->fb) return SGFX_OK; /* already front, nothing to do */
  return sgfx_hal_dsi_flip(bus, timeout_ms);
}

/* ----------------------------------------------------------------- factory */

static int dsi_fmt_map(sgfx_pixfmt_t f, lcd_color_rgb_pixel_format_t* out, uint8_t* bpp){
  switch (f){
    case SGFX_FMT_RGB565: *out = LCD_COLOR_PIXEL_FORMAT_RGB565; *bpp = 2; return SGFX_OK;
    case SGFX_FMT_RGB888: *out = LCD_COLOR_PIXEL_FORMAT_RGB888; *bpp = 3; return SGFX_OK;
    /* RGB666 is 18 bits/pixel in DPI memory layout — not byte-addressable per
     * pixel, so SGFX's memcpy-based direct path cannot describe it. */
    default: return SGFX_ERR_NOSUP;
  }
}

static void dsi_teardown(idf_dsi_ctx_t* c){
  if (!c) return;
  if (c->refresh_sem) vSemaphoreDelete(c->refresh_sem);
  if (c->dpi) esp_lcd_panel_del(c->dpi);
  if (c->io)  esp_lcd_panel_io_del(c->io);
  if (c->dsi) esp_lcd_del_dsi_bus(c->dsi);
  if (c->ldo) esp_ldo_release_channel(c->ldo);
  free(c);
}

int sgfx_hal_make_dsi(sgfx_bus_t* out, const sgfx_hal_cfg_dsi_t* cfg){
  if (!out || !cfg) return SGFX_ERR_INVAL;
  if (cfg->width == 0 || cfg->height == 0) return SGFX_ERR_INVAL;
  if (cfg->lane_mbps == 0 || cfg->dpi_clk_hz == 0) return SGFX_ERR_INVAL;

  lcd_color_rgb_pixel_format_t px_fmt;
  uint8_t bytes_pp = 0;
  int rc = dsi_fmt_map(cfg->fb_fmt, &px_fmt, &bytes_pp);
  if (rc) return rc;

  idf_dsi_ctx_t* c = (idf_dsi_ctx_t*)calloc(1, sizeof(*c));
  if (!c) return SGFX_ERR_NOMEM;
  c->width    = cfg->width;
  c->height   = cfg->height;
  c->bytes_pp = bytes_pp;
  c->pin_rst  = cfg->pin_rst;

  /* 1. DSI PHY power. Must come first: the PHY is in a "no power" state until
   *    its LDO channel is up, and esp_lcd_new_dsi_bus() touches the PHY. */
  if (cfg->ldo_chan >= 0){
    esp_ldo_channel_config_t ldo_cfg = {
      .chan_id    = cfg->ldo_chan,
      .voltage_mv = (int)cfg->ldo_mv,
    };
    if (esp_ldo_acquire_channel(&ldo_cfg, &c->ldo) != ESP_OK){ rc = SGFX_ERR_EIO; goto fail; }
  }

  /* 2. Optional direct panel reset GPIO (-1 on Tab5, see dsi_gpio_set()). */
  if (cfg->pin_rst >= 0){
    gpio_config_t io = { .pin_bit_mask = 1ULL << cfg->pin_rst, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level((gpio_num_t)cfg->pin_rst, 1);
  }

  /* 3. DSI bus (host controller + D-PHY). */
  {
    /* MIPI_DSI_PHY_CLK_SRC_DEFAULT is the spelling that is stable across
     * IDF 5.3..5.5 (in 5.5 it is a compat alias for
     * MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY = PLL_F20M). The newer
     * ..._PLLREF_CLK_SRC_DEFAULT = XTAL only applies to ESP32-P4 rev >= 3.0.
     * M5Stack's own Tab5 BSP uses this same macro. */
    esp_lcd_dsi_bus_config_t bus_cfg = {
      .bus_id             = SGFX_IDF_DSI_BUS_ID,
      .num_data_lanes     = (uint8_t)(cfg->lane_count > 0 ? cfg->lane_count : 0),
      .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = (float)cfg->lane_mbps,
    };
    if (esp_lcd_new_dsi_bus(&bus_cfg, &c->dsi) != ESP_OK){ rc = SGFX_ERR_EIO; goto fail; }
  }

  /* 4. Low-speed DCS command channel ("DBI", generic interface internally). */
  {
    esp_lcd_dbi_io_config_t dbi_cfg = {
      .virtual_channel = SGFX_IDF_DSI_VCHANNEL,
      .lcd_cmd_bits    = 8,
      .lcd_param_bits  = 8,
    };
    if (esp_lcd_new_panel_io_dbi(c->dsi, &dbi_cfg, &c->io) != ESP_OK){ rc = SGFX_ERR_EIO; goto fail; }
  }

  /* 5. Video-mode DPI panel. Creating it allocates the scanout framebuffer in
   *    PSRAM but does not start scanning out — that is esp_lcd_panel_init(),
   *    deferred to sgfx_hal_dsi_start_video() so the panel driver's DCS init
   *    sequence goes out first (the order ESP-IDF's own vendor DSI drivers
   *    use: create DPI panel, send init sequence over DBI, then init DPI). */
  {
    c->num_fbs = (cfg->num_fbs >= 2) ? 2 : 1;
    esp_lcd_dpi_panel_config_t dpi_cfg = {
      .virtual_channel    = SGFX_IDF_DSI_VCHANNEL,
      .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = (float)cfg->dpi_clk_hz / 1000000.0f,
      .pixel_format       = px_fmt,
      .num_fbs            = c->num_fbs,
      .video_timing = {
        .h_size            = cfg->width,
        .v_size            = cfg->height,
        .hsync_pulse_width = cfg->hsync,
        .hsync_back_porch  = cfg->hbp,
        .hsync_front_porch = cfg->hfp,
        .vsync_pulse_width = cfg->vsync,
        .vsync_back_porch  = cfg->vbp,
        .vsync_front_porch = cfg->vfp,
      },
      /* Single-buffer (default): CPU writes straight into the framebuffer,
       * the 2D DMA copy engine is only used by esp_lcd_panel_draw_bitmap(),
       * which that path never calls, so use_dma2d stays 0. Double-buffer
       * (num_fbs==2): sgfx_hal_dsi_flip() DOES call draw_bitmap() (to switch
       * the live buffer), and use_dma2d=true is required for that call to
       * work at all (see ESP-IDF's examples/peripherals/camera/
       * common_components/dsi_init), even though the buffer-switch case
       * doesn't do a real pixel copy. */
      .flags = { .use_dma2d = (c->num_fbs >= 2) ? 1u : 0u, .disable_lp = 0 },
    };
    if (esp_lcd_new_panel_dpi(c->dsi, &dpi_cfg, &c->dpi) != ESP_OK){ rc = SGFX_ERR_EIO; goto fail; }
  }

  /* 6. Grab the driver-allocated scanout buffer(s), and for double-buffering,
   * the completion semaphore + callback sgfx_hal_dsi_flip() waits on. */
  {
    void* fb0 = NULL;
    void* fb1 = NULL;
    esp_err_t fb_rc = (c->num_fbs >= 2)
      ? esp_lcd_dpi_panel_get_frame_buffer(c->dpi, 2, &fb0, &fb1)
      : esp_lcd_dpi_panel_get_frame_buffer(c->dpi, 1, &fb0);
    if (fb_rc != ESP_OK || !fb0 || (c->num_fbs >= 2 && !fb1)){
      rc = SGFX_ERR_EIO; goto fail;
    }
    c->fb        = (uint8_t*)fb0;
    c->fb1       = (uint8_t*)fb1; /* NULL when num_fbs<2 */
    c->active_fb = c->fb;
    c->fb_stride = (size_t)cfg->width * (size_t)bytes_pp;
    c->fb_bytes  = c->fb_stride * (size_t)cfg->height;

    if (c->num_fbs >= 2){
      c->refresh_sem = xSemaphoreCreateBinary();
      if (!c->refresh_sem){ rc = SGFX_ERR_NOMEM; goto fail; }
      esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_refresh_done = dsi_on_refresh_done };
      if (esp_lcd_dpi_panel_register_event_callbacks(c->dpi, &cbs, c) != ESP_OK){
        rc = SGFX_ERR_EIO; goto fail;
      }
    }
  }

  out->ops      = &IDF_DSI_OPS;
  out->user     = c;
  out->hz_max   = cfg->dpi_clk_hz;
  out->features = SGFX_CAP_DSI;
  return SGFX_OK;

fail:
  dsi_teardown(c);
  return rc;
}

#else  /* !SOC_MIPI_DSI_SUPPORTED */

/* This SoC has no MIPI-DSI peripheral. Keep the translation unit non-empty
 * (ISO C forbids an empty one) without emitting any symbol. */
typedef int sgfx_espidf_dsi_unsupported_t;

#endif /* SOC_MIPI_DSI_SUPPORTED */

#endif /* SGFX_HAL_ESPIDF */
