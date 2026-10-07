
#pragma once
/* sgfx_port.h — glue that reads PlatformIO build flags and constructs the bus+driver */
#include "sgfx.h"
#include "sgfx_hal.h"
#include <string.h>
#include <stdlib.h>

/* --- Required basics --- */
#ifndef SGFX_W
# error "Define SGFX_W (display width) via build_flags."
#endif
#ifndef SGFX_H
# error "Define SGFX_H (display height) via build_flags."
#endif
#ifndef SGFX_ROT
# define SGFX_ROT 0
#endif

#if !defined(SGFX_BUS_SPI) && !defined(SGFX_BUS_I2C) && !defined(SGFX_BUS_DSI) && !defined(SGFX_BUS_8080) && !defined(SGFX_BUS_RGB)
# error "Select one bus: -DSGFX_BUS_SPI, -DSGFX_BUS_I2C or -DSGFX_BUS_DSI (8080/RGB reserved)"
#endif

#if !defined(SGFX_DRV_ST7789) && !defined(SGFX_DRV_ST7735) && !defined(SGFX_DRV_ST7796) && !defined(SGFX_DRV_SSD1306) && !defined(SGFX_DRV_ILI9341) && !defined(SGFX_DRV_SH1107) && !defined(SGFX_DRV_GC9A01) && !defined(SGFX_DRV_UC8151) && !defined(SGFX_DRV_ILI9881C) && !defined(SGFX_DRV_ST7121) && !defined(SGFX_DRV_ST7123) && !defined(SGFX_DRV_TAB5_AUTO)
# error "Select a display driver: e.g. -DSGFX_DRV_ST7789"
#endif

#ifndef SGFX_STRICT_RGB565
#define SGFX_STRICT_RGB565 1
#endif

/* --- MIPI-DSI build-flag defaults ---
 * Only the ones with no safe universal value are left undefined (and will
 * fail the build if SGFX_BUS_DSI is selected without them).
 */
#if defined(SGFX_BUS_DSI)
# ifndef SGFX_DSI_LANES
#   define SGFX_DSI_LANES 2
# endif
# ifndef SGFX_DSI_MBPS
#   error "Define SGFX_DSI_MBPS (per-lane DSI bitrate, Mbps) via build_flags."
# endif
# ifndef SGFX_DSI_CLK_HZ
#   error "Define SGFX_DSI_CLK_HZ (DPI pixel clock, Hz) via build_flags."
# endif
# ifndef SGFX_DSI_FMT
#   define SGFX_DSI_FMT SGFX_FMT_RGB565
# endif
# ifndef SGFX_PIN_RST
    /* -1 == panel reset is not a direct GPIO (e.g. behind an I2C expander) */
#   define SGFX_PIN_RST (-1)
# endif
# ifndef SGFX_DSI_LDO_CHAN
#   define SGFX_DSI_LDO_CHAN (-1)
# endif
# ifndef SGFX_DSI_LDO_MV
#   define SGFX_DSI_LDO_MV 2500
# endif
# if !defined(SGFX_DSI_HSYNC) || !defined(SGFX_DSI_HBP) || !defined(SGFX_DSI_HFP) || \
     !defined(SGFX_DSI_VSYNC) || !defined(SGFX_DSI_VBP) || !defined(SGFX_DSI_VFP)
#   error "Define SGFX_DSI_HSYNC/_HBP/_HFP and SGFX_DSI_VSYNC/_VBP/_VFP via build_flags."
# endif
#endif

/* --- Driver registry decls --- */
#if defined(SGFX_DRV_ST7789)
  extern const sgfx_driver_ops_t sgfx_st7789_ops;
  extern const sgfx_caps_t       sgfx_st7789_caps_default;
# define SGFX__DRV_OPS  (&sgfx_st7789_ops)
# define SGFX__DRV_CAPS (&sgfx_st7789_caps_default)
#elif defined(SGFX_DRV_SSD1306)
  extern const sgfx_driver_ops_t sgfx_ssd1306_ops;
  extern const sgfx_caps_t       sgfx_ssd1306_caps_128x64;
# define SGFX__DRV_OPS  (&sgfx_ssd1306_ops)
# define SGFX__DRV_CAPS (&sgfx_ssd1306_caps_128x64)
#elif defined(SGFX_DRV_ST7735)
  extern const sgfx_driver_ops_t sgfx_st7735_ops;
  extern const sgfx_caps_t       sgfx_st7735_caps;
# define SGFX__DRV_OPS  (&sgfx_st7735_ops)
# define SGFX__DRV_CAPS (&sgfx_st7735_caps)
#elif defined(SGFX_DRV_ST7796)
  extern const sgfx_driver_ops_t sgfx_st7796_ops;
  extern const sgfx_caps_t       sgfx_st7796_caps;
# define SGFX__DRV_OPS  (&sgfx_st7796_ops)
# define SGFX__DRV_CAPS (&sgfx_st7796_caps)
#elif defined(SGFX_DRV_ILI9881C)
  extern const sgfx_driver_ops_t sgfx_ili9881c_ops;
  extern const sgfx_caps_t       sgfx_ili9881c_caps_default;
# define SGFX__DRV_OPS  (&sgfx_ili9881c_ops)
# define SGFX__DRV_CAPS (&sgfx_ili9881c_caps_default)
#elif defined(SGFX_DRV_ST7121)
  extern const sgfx_driver_ops_t sgfx_st7121_ops;
  extern const sgfx_caps_t       sgfx_st7121_caps_default;
# define SGFX__DRV_OPS  (&sgfx_st7121_ops)
# define SGFX__DRV_CAPS (&sgfx_st7121_caps_default)
#elif defined(SGFX_DRV_ST7123)
  extern const sgfx_driver_ops_t sgfx_st7123_ops;
  extern const sgfx_caps_t       sgfx_st7123_caps_default;
# define SGFX__DRV_OPS  (&sgfx_st7123_ops)
# define SGFX__DRV_CAPS (&sgfx_st7123_caps_default)
#elif defined(SGFX_DRV_TAB5_AUTO)
  /* Tab5 runtime autodetect meta-driver (src/drivers/dsi_tab5_autodetect.c).
   * Selecting this alone is not sufficient to link: the meta-driver calls
   * directly into ili9881c.c/st7121.c/st7123.c's ops tables, so a Tab5
   * SGFX_DRV_TAB5_AUTO build must also define SGFX_DRV_ILI9881C,
   * SGFX_DRV_ST7121 and SGFX_DRV_ST7123 (all four flags together) so those
   * three files get compiled in alongside this one. */
  extern const sgfx_driver_ops_t sgfx_tab5_auto_ops;
  extern const sgfx_caps_t       sgfx_tab5_auto_caps_default;
# define SGFX__DRV_OPS  (&sgfx_tab5_auto_ops)
# define SGFX__DRV_CAPS (&sgfx_tab5_auto_caps_default)
#else
# error "Selected driver not yet wired in sgfx_port.h"
#endif

/* --- Autoinit convenience --- */
/*
 * sgfx_autoinit — one-shot helper for single-display projects.
 *
 * Allocates a sgfx_bus_t on the heap (consistent with sgfx_open_spi/i2c)
 * so the device holds a stable pointer after this function returns.
 * The bus is freed automatically if driver init fails.
 *
 * For multi-display or custom bring-up use sgfx_open_spi / sgfx_open_i2c
 * directly instead.
 */
static inline int sgfx_autoinit(sgfx_device_t* dev, void* scratch, size_t scratch_len) {
  sgfx_bus_t* bus = (sgfx_bus_t*)calloc(1, sizeof(sgfx_bus_t));
  if (!bus) return SGFX_ERR_NOMEM;

#if defined(SGFX_BUS_SPI)
  sgfx_hal_cfg_spi_t cfg = {
    .pin_sck  = SGFX_PIN_SCK,
    .pin_mosi = SGFX_PIN_MOSI,
    .pin_miso = SGFX_PIN_MISO,
    .pin_cs   = SGFX_PIN_CS,
    .pin_dc   = SGFX_PIN_DC,
    .pin_rst  = SGFX_PIN_RST,
    .pin_bl   = SGFX_PIN_BL,
    .hz       = SGFX_SPI_HZ
  };
  if (sgfx_hal_make_spi(bus, &cfg) < 0) { free(bus); return -1; }
#elif defined(SGFX_BUS_I2C)
  sgfx_hal_cfg_i2c_t cfg = {
    .pin_sda = SGFX_PIN_SDA,
    .pin_scl = SGFX_PIN_SCL,
    .pin_rst = SGFX_PIN_RST,
    .pin_bl  = SGFX_PIN_BL,
    .addr    = SGFX_I2C_ADDR,
    .hz      = SGFX_I2C_HZ
  };
  if (sgfx_hal_make_i2c(bus, &cfg) < 0) { free(bus); return -1; }
#elif defined(SGFX_BUS_DSI)
  sgfx_hal_cfg_dsi_t cfg = {
    .lane_count = SGFX_DSI_LANES,
    .lane_mbps  = SGFX_DSI_MBPS,
    .width      = SGFX_W,
    .height     = SGFX_H,
    .fb_fmt     = SGFX_DSI_FMT,
    .pin_rst    = SGFX_PIN_RST,
    .ldo_chan   = SGFX_DSI_LDO_CHAN,
    .ldo_mv     = SGFX_DSI_LDO_MV,
    .dpi_clk_hz = SGFX_DSI_CLK_HZ,
    .hsync      = SGFX_DSI_HSYNC,
    .hbp        = SGFX_DSI_HBP,
    .hfp        = SGFX_DSI_HFP,
    .vsync      = SGFX_DSI_VSYNC,
    .vbp        = SGFX_DSI_VBP,
    .vfp        = SGFX_DSI_VFP
  };
  if (sgfx_hal_make_dsi(bus, &cfg) < 0) { free(bus); return -1; }
#else
# error "BUS not implemented in autoinit"
#endif

  sgfx_caps_t caps = *SGFX__DRV_CAPS;
  caps.width  = SGFX_W;
  caps.height = SGFX_H;
  int rc = sgfx_init(dev, bus, SGFX__DRV_OPS, &caps, scratch, scratch_len);
  if (rc) { free(bus); return rc; }
  sgfx_set_rotation(dev, SGFX_ROT);
  return 0;
}
