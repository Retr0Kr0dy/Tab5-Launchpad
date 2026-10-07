
#pragma once
/*
 * SGFX — st4lk3r GFX: MCU-agnostic, module-agnostic tiny C99 graphics core
 * Single public header. Pair with sgfx_port.h for build-flag autoconfig.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ---------- Logging (no-op by default) ----------
#ifndef SGFX_LOG
# define SGFX_LOG(...) do{}while(0)
#endif

// Optional compile-time config header hook
#ifdef SGFX_USE_CONFIG_H
# include "sgfx_config.h"
#endif
typedef struct sgfx_hal_cfg_spi sgfx_hal_cfg_spi_t;
typedef struct sgfx_hal_cfg_i2c sgfx_hal_cfg_i2c_t;
typedef struct sgfx_hal_cfg_dsi sgfx_hal_cfg_dsi_t;


#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Error codes ---------- */
#define SGFX_OK          0
#define SGFX_ERR_EIO    -5
#define SGFX_ERR_INVAL  -22
#define SGFX_ERR_NOMEM  -12
#define SGFX_ERR_NOSUP  -95

/* ---------- Pixel formats ---------- */
typedef enum {
  SGFX_FMT_MONO1 = 0,
  SGFX_FMT_GRAY2,
  SGFX_FMT_INDEXED4,
  SGFX_FMT_RGB565,
  SGFX_FMT_RGB666,
  SGFX_FMT_RGB888,
  SGFX_FMT_ARGB8888
} sgfx_pixfmt_t;

typedef struct { uint8_t r,g,b,a; } sgfx_rgba8_t;

typedef struct {
  sgfx_rgba8_t colors[256];
  uint8_t size;
} sgfx_palette_t;

typedef struct { int16_t x, y, w, h; } sgfx_rect_t;

/* ---------- Forward decls ---------- */
typedef struct sgfx_bus sgfx_bus_t;
typedef struct sgfx_device sgfx_device_t;

/* ---------- Bus ops (HAL) ---------- */
/* GPIO ids used by driver -> HAL calls.
 * Keep this tiny and transport-neutral: CS is owned by bus write paths, while
 * drivers may request only side-band control lines.
 */
enum {
  SGFX_GPIO_DC  = 0,
  SGFX_GPIO_BL  = 1,
  SGFX_GPIO_RST = 2
};

typedef struct {
  int  (*begin)(sgfx_bus_t*);                             /* claim bus / set CS */
  void (*end)(sgfx_bus_t*);                               /* release bus */
  int  (*write_cmd)(sgfx_bus_t*, uint8_t cmd);            /* send one command */
  int  (*write_data)(sgfx_bus_t*, const void* buf, size_t len); /* send data bytes */
  int  (*write_repeat)(sgfx_bus_t*, const void* unit, size_t unit_bytes, size_t count);
  int  (*write_pixels)(sgfx_bus_t*, const void* px, size_t count, sgfx_pixfmt_t src_fmt);
  int  (*read_data)(sgfx_bus_t*, void* buf, size_t len);  /* optional */
  void (*delay_ms)(sgfx_bus_t*, uint32_t ms);
  void (*gpio_set)(sgfx_bus_t*, int pin_id, bool level);  /* SGFX_GPIO_* side-band pins */
} sgfx_bus_ops_t;

struct sgfx_bus {
  const sgfx_bus_ops_t* ops;
  void* user;
  uint32_t hz_max;
  uint32_t features;
};

/* ---------- Driver contracts ---------- */
typedef struct {
  uint16_t width, height;
  sgfx_pixfmt_t native_fmt;
  uint8_t bpp;
  uint32_t caps;
} sgfx_caps_t;

typedef struct {
  int  (*init)(sgfx_device_t*);
  void (*reset)(sgfx_device_t*);
  int  (*set_rotation)(sgfx_device_t*, uint8_t rot); /* 0..3 */
  int  (*set_window)(sgfx_device_t*, int x, int y, int w, int h);
  int  (*write_pixels)(sgfx_device_t*, const void* px, size_t count, sgfx_pixfmt_t src_fmt);
  int  (*fill_rect)(sgfx_device_t*, int x, int y, int w, int h, sgfx_rgba8_t c); /* optional */
  int  (*power)(sgfx_device_t*, bool on);
  int  (*invert)(sgfx_device_t*, bool on);
  int  (*brightness)(sgfx_device_t*, uint8_t pct);
  int  (*present)(sgfx_device_t*);

  /* ---- Direct-framebuffer panels (MIPI-DSI / parallel-RGB style) ----
   * Both optional; NULL on every command-and-data (SPI/I2C, MIPI-DBI) panel.
   *
   * get_fb_ptr    Return a pointer to the hardware scanout buffer the display
   *               controller DMAs out continuously, or NULL if this panel has
   *               no such buffer. *out_stride_bytes receives the buffer's row
   *               pitch in bytes (may differ from width*bpp). Must be cheap and
   *               side-effect free: no bus traffic, no hardware I/O.
   * flush_surface Notify the driver that the CPU wrote pixels into the rect
   *               (x,y,w,h) of that buffer, so it can cache-writeback / poke the
   *               scanout hardware as needed. Returns SGFX_OK on success.
   *               NULL means "nothing to do", not an error.
   */
  void* (*get_fb_ptr)(sgfx_device_t*, size_t* out_stride_bytes);
  int   (*flush_surface)(sgfx_device_t*, int x, int y, int w, int h);
} sgfx_driver_ops_t;

/* caps flags */
enum {
  SGFX_CAP_PARTIAL   = 1u<<0,
  SGFX_CAP_READBACK  = 1u<<1,
  SGFX_CAP_SCROLL    = 1u<<2,
  SGFX_CAP_INVERT    = 1u<<3,
  SGFX_CAP_HW_FILL   = 1u<<4,
  SGFX_CAP_EPD       = 1u<<5,
  SGFX_CAP_RGB_IF    = 1u<<6,   /* reserved: parallel-RGB (DPI) transport */
  SGFX_CAP_ROUND     = 1u<<7,
  SGFX_CAP_DSI       = 1u<<8    /* MIPI-DSI transport w/ direct scanout FB */
};

/* ---------- Device object ---------- */
struct sgfx_device {
  sgfx_bus_t*              bus;
  const sgfx_driver_ops_t* drv;
  sgfx_caps_t              caps;
  sgfx_rect_t              clip;
  uint8_t                  rotation;
  void*                    scratch;
  size_t                   scratch_bytes;
  sgfx_palette_t           palette;
  uint8_t                  dither;
  int                      mirror; 
};

/* ---------- Core API ---------- */
int  sgfx_init(sgfx_device_t* dev, sgfx_bus_t* bus,
               const sgfx_driver_ops_t* drv, const sgfx_caps_t* caps,
               void* scratch_buf, size_t scratch_len);

void sgfx_set_clip(sgfx_device_t*, sgfx_rect_t r);
void sgfx_reset_clip(sgfx_device_t*);

void sgfx_set_rotation(sgfx_device_t*, uint8_t rot);
void sgfx_set_palette(sgfx_device_t*, const sgfx_palette_t* pal);
void sgfx_set_dither(sgfx_device_t*, uint8_t mode);

int  sgfx_clear(sgfx_device_t*, sgfx_rgba8_t color);
int  sgfx_draw_pixel(sgfx_device_t*, int x, int y, sgfx_rgba8_t c);
int  sgfx_fill_rect(sgfx_device_t*, int x, int y, int w, int h, sgfx_rgba8_t c);
int  sgfx_present(sgfx_device_t*);

/* Blit (optional minimal) */
int  sgfx_blit(sgfx_device_t*, int x,int y, int w,int h,
               sgfx_pixfmt_t src_fmt, const void* pixels, size_t pitch_bytes);

/* Tiny text (optional) */
int  sgfx_text8x8(sgfx_device_t*, int x,int y, const char* ascii, sgfx_rgba8_t c);

int  sgfx_draw_fast_hline(sgfx_device_t*, int x,int y,int w, sgfx_rgba8_t c);
int  sgfx_draw_fast_vline(sgfx_device_t*, int x,int y,int h, sgfx_rgba8_t c);
int  sgfx_draw_rect(sgfx_device_t*, int x,int y,int w,int h, sgfx_rgba8_t c);

/* compact 5x7 ASCII (A-Z, 0-9, space) */
int  sgfx_text5x7(sgfx_device_t*, int x,int y, const char* ascii, sgfx_rgba8_t c);


int sgfx_text5x7_scaled(sgfx_device_t* d, int x, int y,
                        const char* s, sgfx_rgba8_t c,
                        int sx, int sy);

int sgfx_text8x8_scaled(sgfx_device_t* d, int x, int y,
                        const char* s, sgfx_rgba8_t c,
                        int sx, int sy);

#ifdef __cplusplus
}
#endif


/* ---------- Minimal factory helpers ---------- */
int sgfx_open_spi(struct sgfx_device* dev,
                  const sgfx_hal_cfg_spi_t* bus_cfg,
                  const sgfx_driver_ops_t* drv,
                  const void* drv_cfg);

int sgfx_open_i2c(struct sgfx_device* dev,
                  const sgfx_hal_cfg_i2c_t* bus_cfg,
                  const sgfx_driver_ops_t* drv,
                  const void* drv_cfg);

int sgfx_open_dsi(struct sgfx_device* dev,
                  const sgfx_hal_cfg_dsi_t* bus_cfg,
                  const sgfx_driver_ops_t* drv,
                  const void* drv_cfg);

