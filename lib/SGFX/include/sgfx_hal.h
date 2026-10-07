
#pragma once
#include "sgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sgfx_hal_cfg_spi {
  int pin_sck, pin_mosi, pin_miso, pin_cs, pin_dc, pin_rst, pin_bl;
  uint32_t hz;
} sgfx_hal_cfg_spi_t;

typedef struct sgfx_hal_cfg_i2c {
  int pin_sda, pin_scl, pin_rst, pin_bl;
  uint8_t addr;
  uint32_t hz;
} sgfx_hal_cfg_i2c_t;

/* MIPI-DSI (video mode): the SoC's DSI/DPI peripheral continuously scans out a
 * RAM-resident framebuffer. There is no CASET/RASET/RAMWR windowing; the CPU
 * writes pixels straight into that buffer and then asks the HAL to make them
 * visible to the DMA engine (see sgfx_hal_dsi_get_fb / sgfx_hal_dsi_flush).
 */
typedef struct sgfx_hal_cfg_dsi {
  int      lane_count;        /* number of DSI data lanes (Tab5: 2) */
  uint32_t lane_mbps;         /* per-lane bitrate (Tab5: 900 or 1040 depending on panel) */
  uint16_t width, height;     /* panel resolution in pixels (Tab5: 720x1280) */
  sgfx_pixfmt_t fb_fmt;       /* pixel format of the scanout buffer, e.g. SGFX_FMT_RGB565 */
  int      pin_rst;           /* panel reset GPIO, or -1 if reset is handled externally
                                 (Tab5: -1, reset is via an I2C GPIO expander bit) */
  int      ldo_chan;          /* on-chip LDO channel powering the DSI PHY, or -1 to skip (Tab5: 3) */
  uint32_t ldo_mv;            /* LDO output voltage in mV, ignored if ldo_chan < 0 (Tab5: 2500) */
  uint32_t dpi_clk_hz;        /* DPI pixel clock */
  uint16_t hsync, hbp, hfp;   /* horizontal sync/back-porch/front-porch, in pixel clocks */
  uint16_t vsync, vbp, vfp;   /* vertical sync/back-porch/front-porch, in lines */
  /* 0 or 1 (default, zero-initialized structs stay on the single-buffer
   * path) = one scanout framebuffer. 2 = allocate a second driver-owned
   * framebuffer and enable sgfx_hal_dsi_get_back_fb()/sgfx_hal_dsi_flip()
   * below (real double-buffering, backed by ESP-IDF's own
   * esp_lcd_dpi_panel num_fbs field). */
  uint8_t  num_fbs;
} sgfx_hal_cfg_dsi_t;

/* Factories implemented by HALs */
int sgfx_hal_make_spi(sgfx_bus_t* out, const sgfx_hal_cfg_spi_t* cfg);
int sgfx_hal_make_i2c(sgfx_bus_t* out, const sgfx_hal_cfg_i2c_t* cfg);
int sgfx_hal_make_dsi(sgfx_bus_t* out, const sgfx_hal_cfg_dsi_t* cfg);

/* ---- DSI-only bus accessors -------------------------------------------
 * sgfx_bus_ops_t is a byte-stream (MIPI-DBI) contract and has nowhere to
 * express "here is the scanout buffer". These three named entry points are
 * the bridge a DSI panel driver uses to implement sgfx_driver_ops_t's
 * get_fb_ptr / flush_surface / init on top of an sgfx_bus_t made by
 * sgfx_hal_make_dsi(). They all take the bus the driver already holds
 * (dev->bus), so no vendor types leak into any public header.
 *
 * All three are no-ops / NULL on a bus that was not made by
 * sgfx_hal_make_dsi(); they check bus->ops before touching bus->user.
 */

/* Start the DPI video stream (begins continuous scanout of the framebuffer).
 * MUST be called only AFTER the panel's DCS init sequence has been pushed
 * through the bus's write_cmd/write_data ops -- panel ICs expect their init
 * sequence in LP command mode before video data starts arriving. Idempotent.
 */
int sgfx_hal_dsi_start_video(sgfx_bus_t* bus);

/* Scanout framebuffer + its row pitch in bytes. Cheap, no hardware access. */
void* sgfx_hal_dsi_get_fb(sgfx_bus_t* bus, size_t* out_stride_bytes);

/* Make CPU writes to (x,y,w,h) of the scanout buffer visible to the DMA engine. */
int sgfx_hal_dsi_flush(sgfx_bus_t* bus, int x, int y, int w, int h);

/* ---- Real double-buffering (sgfx_hal_cfg_dsi_t.num_fbs == 2 only) -----
 * Lets a producer (e.g. a hardware scale/rotate engine) write a whole
 * converted frame directly into the buffer NOT currently being scanned
 * out, then a real hardware buffer switch (esp_lcd_panel_draw_bitmap() on
 * a driver-owned pointer, which the DPI driver recognizes and switches to
 * rather than copying into) makes it live -- no CPU blit, no cache-flush-
 * then-DMA-catch-up race like the single-buffer sgfx_hal_dsi_get_fb()/
 * flush() path has. On a bus made with num_fbs 0 or 1, both functions
 * below degrade to safe single-buffer behavior (get_back_fb() returns the
 * one and only framebuffer, flip() is a no-op returning SGFX_OK) rather
 * than failing, so callers that don't care about double-buffering can use
 * either path uniformly. */

/* The framebuffer NOT currently live (safe to write a whole new frame into
 * without racing the DMA scanout). Cheap, no hardware access. */
void* sgfx_hal_dsi_get_back_fb(sgfx_bus_t* bus, size_t* out_stride_bytes);

/* Make the buffer last returned by sgfx_hal_dsi_get_back_fb() the new live
 * scanout buffer (a real hardware pointer switch, not a copy). If
 * timeout_ms > 0, blocks until the DPI driver's on_refresh_done callback
 * confirms the switch has actually taken effect on screen (bounded by
 * timeout_ms; SGFX_ERR_EIO on timeout) -- pass 0 to fire-and-forget. */
int sgfx_hal_dsi_flip(sgfx_bus_t* bus, uint32_t timeout_ms);

/* Same hardware buffer switch as sgfx_hal_dsi_flip(), but skips the
 * CPU-cache writeback. Use ONLY when the back buffer was produced by DMA/PPA
 * and the CPU has not modified it since that hardware write. This avoids an
 * otherwise pointless full-frame PSRAM cache walk on the video fast path. */
int sgfx_hal_dsi_flip_hw_written(sgfx_bus_t* bus, uint32_t timeout_ms);

/* Ensure buffer 0 (the one every normal SGFX drawing call always targets)
 * is the live/scanned-out buffer, flipping to it if it currently isn't.
 * No-op if it already is, or if this bus wasn't made with num_fbs==2. Call
 * once when done with a temporary get_back_fb()/flip() session (e.g. the
 * video player, on exit) so normal drawing becomes visible again. */
int sgfx_hal_dsi_flip_to_front(sgfx_bus_t* bus, uint32_t timeout_ms);

/* DIAGNOSTIC ONLY, ILI9881C-specific: reads back the panel's Page-1 ID
 * registers (0x00/0x01/0x02) via esp_lcd_panel_io_rx_param(), the same call
 * the real esp_lcd_ili9881c component makes. Confirms whether the low-speed
 * DSI command channel is genuinely being understood by the panel (this bus
 * has no general-purpose read_data path -- see espidf_dsi.c's header).
 * Returns SGFX_OK if all three reads succeeded; *id1, *id2, *id3 are set
 * to 0xEE if a read failed, so a partial failure is visible even on error. */
int sgfx_hal_dsi_debug_read_ili9881c_id(sgfx_bus_t* bus, uint8_t* id1, uint8_t* id2, uint8_t* id3);

#ifdef __cplusplus
}
#endif
