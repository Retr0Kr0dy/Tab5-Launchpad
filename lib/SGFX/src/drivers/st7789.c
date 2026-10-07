#ifdef SGFX_DRV_ST7789

#include "sgfx.h"
#include "st77xx_common.h"
#include "sgfx_port.h"
#include <stdint.h>
#include <stdlib.h>

/* ========== ST77xx Commands ========== */
#define ST77_SWRESET   0x01
#define ST77_SLPIN     0x10
#define ST77_SLPOUT    0x11
#define ST77_INVOFF    0x20
#define ST77_INVON     0x21
#define ST77_DISPON    0x29
#define ST77_CASET     0x2A
#define ST77_RASET     0x2B
#define ST77_RAMWR     0x2C
#define ST77_MADCTL    0x36
#define ST77_COLMOD    0x3A
#define ST77_NORON     0x13

/* MADCTL bits */
#define MADCTL_MY   0x80
#define MADCTL_MX   0x40
#define MADCTL_MV   0x20
#define MADCTL_ML   0x10
#define MADCTL_BGR  0x08
#define MADCTL_MH   0x04

/* ========== Offset aliases ========== */
/* Accept both naming styles; default to 0 if not given. */
#ifndef SGFX_ST77XX_XOFF
# ifdef SGFX_ST7789_COLSTART
#  define SGFX_ST77XX_XOFF SGFX_ST7789_COLSTART
# else
#  define SGFX_ST77XX_XOFF 0
# endif
#endif
#ifndef SGFX_ST77XX_YOFF
# ifdef SGFX_ST7789_ROWSTART
#  define SGFX_ST77XX_YOFF SGFX_ST7789_ROWSTART
# else
#  define SGFX_ST77XX_YOFF 0
# endif
#endif

/* ========== Inversion mode ========== */
/* Some ST77xx/ST7789 panels need INVON for correct colors.
 * Symptom when wrong: black appears white and warm colors appear cyan/bluish.
 *
 * Generic flag:      SGFX_ST77XX_INVERT=1
 * ST7789-only flag:  SGFX_ST7789_INVERT=1
 */
#ifndef SGFX_ST77XX_INVERT
# define SGFX_ST77XX_INVERT 0
#endif
#ifndef SGFX_ST7789_INVERT
# define SGFX_ST7789_INVERT SGFX_ST77XX_INVERT
#endif

/* ========== Private state ========== */
typedef struct {
  uint16_t xoff_portrait;   /* portrait-native offsets as provided by flags */
  uint16_t yoff_portrait;
  uint8_t  madctl_base;     /* usually BGR */
  uint8_t  rot;             /* 0..3 */
} st_priv_t;

static st_priv_t g_st;


static inline int st_send(sgfx_device_t* d, uint8_t cmd, const void* data, size_t n){
  if (d->bus->ops->write_cmd(d->bus, cmd)) return -1;
  if (n && data) return d->bus->ops->write_data(d->bus, data, n);
  return 0;
}

static inline uint16_t pack565(sgfx_rgba8_t c){
  return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

/* Get effective offsets for current rotation:
 * We treat XOFF/YOFF as PORTRAIT values; for ROT=1/3 (landscape) swap axes.
 */
static inline void st_effective_offsets(uint8_t rot, uint16_t* xo, uint16_t* yo){
  uint16_t x = SGFX_ST77XX_XOFF;
  uint16_t y = SGFX_ST77XX_YOFF;
  if (rot & 1){ /* 1 or 3 */
    *xo = y; *yo = x;
  } else {
    *xo = x; *yo = y;
  }
}

/* ========== Driver ops ========== */

static int st_init(sgfx_device_t* d){
  /* Optional hard reset */
  if (d->bus->ops->gpio_set){
    d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, 1);
    if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
    d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, 0);
    if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
    d->bus->ops->gpio_set(d->bus, SGFX_GPIO_RST, 1);
    if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 120);
  }

  /* Soft reset + wake */
  st_send(d, ST77_SWRESET, NULL, 0);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 120);
  st_send(d, ST77_SLPOUT, NULL, 0);
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 120);

  /* Pixel format = RGB565 (0x55) */
  uint8_t colmod = 0x55;
  st_send(d, ST77_COLMOD, &colmod, 1);

  /* Base MADCTL: configurable BGR; most ST7789V2 panels (e.g. Cardputer) use RGB order */
#ifdef SGFX_ST7789_BGR
  g_st.madctl_base = MADCTL_BGR;
#else
  g_st.madctl_base = 0;
#endif

  /* Default modes */
#if SGFX_ST7789_INVERT
  st_send(d, ST77_INVON, NULL, 0);
#else
  st_send(d, ST77_INVOFF, NULL, 0);
#endif
  st_send(d, ST77_NORON,  NULL, 0);
  st_send(d, ST77_DISPON, NULL, 0);

  /* Backlight on if wired */
  if (d->bus->ops->gpio_set) d->bus->ops->gpio_set(d->bus, SGFX_GPIO_BL, 1);

  /* Cache portrait offsets; rotation applied in set_rotation/set_window */
  g_st.xoff_portrait = SGFX_ST77XX_XOFF;
  g_st.yoff_portrait = SGFX_ST77XX_YOFF;
  g_st.rot = 0; /* core will call set_rotation() */

  return 0;
}

static int st_set_rotation(sgfx_device_t* d, uint8_t rot){
  (void)d;
  g_st.rot = (uint8_t)(rot & 3);

  /* Compose MADCTL by rotation (matching Adafruit/TFT_eSPI map) */
  uint8_t mad = g_st.madctl_base;
  switch (g_st.rot){
    case 0: mad |= (MADCTL_MX | MADCTL_MY); break;      /* portrait   */
    case 1: mad |= (MADCTL_MY | MADCTL_MV); break;      /* landscape  */
    case 2: mad |= 0;                         break;    /* portrait 180 */
    case 3: mad |= (MADCTL_MX | MADCTL_MV); break;      /* landscape 180 */
  }
  return st_send(d, ST77_MADCTL, &mad, 1);
}

static int st_set_window(sgfx_device_t* d, int x, int y, int w, int h){
  uint16_t xo, yo; st_effective_offsets(g_st.rot, &xo, &yo);

  uint16_t x0 = (uint16_t)(x + xo);
  uint16_t y0 = (uint16_t)(y + yo);
  uint16_t x1 = (uint16_t)(x + w - 1 + xo);
  uint16_t y1 = (uint16_t)(y + h - 1 + yo);

  uint8_t buf[4];

  /* Column address */
  buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)(x0 & 0xFF);
  buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)(x1 & 0xFF);
  if (st_send(d, ST77_CASET, buf, 4)) return -1;

  /* Row address */
  buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)(y0 & 0xFF);
  buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)(y1 & 0xFF);
  if (st_send(d, ST77_RASET, buf, 4)) return -1;

  /* Write RAM */
  if (d->bus->ops->write_cmd(d->bus, ST77_RAMWR)) return -1;
  return 0;
}

static int st_write_pixels(sgfx_device_t* d, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
#ifdef SGFX_RGB565_BYTESWAP
  if (src_fmt == SGFX_FMT_RGB565) {
    /* Panel expects big-endian RGB565; ESP32 stores uint16_t little-endian → swap bytes. */
#ifndef SGFX_SPI_SWAP_BUF_BYTES
#define SGFX_SPI_SWAP_BUF_BYTES 4096
#endif
    static uint8_t swap_buf[SGFX_SPI_SWAP_BUF_BYTES];
    const uint8_t* s = (const uint8_t*)px;
    size_t remaining = count;
    while (remaining) {
      size_t npx = remaining;
      size_t maxpx = sizeof(swap_buf) / 2;
      if (npx > maxpx) npx = maxpx;
      for (size_t i = 0; i < npx; ++i) {
        swap_buf[2*i]   = s[2*i+1];
        swap_buf[2*i+1] = s[2*i];
      }
      int r = d->bus->ops->write_data(d->bus, swap_buf, npx * 2);
      if (r) return r;
      s += npx * 2;
      remaining -= npx;
    }
    return 0;
  }
#endif
  /* Preferred path: bus supports write_pixels for RGB565 */
  if (src_fmt == SGFX_FMT_RGB565 && d->bus->ops->write_pixels)
    return d->bus->ops->write_pixels(d->bus, px, count, src_fmt);

  /* Fallback: send raw bytes */
  size_t bytes = (src_fmt == SGFX_FMT_RGB565) ? count * 2 : count;
  return d->bus->ops->write_data(d->bus, px, bytes);
}

static int st_fill_rect(sgfx_device_t* d, int x, int y, int w, int h, sgfx_rgba8_t c){
  if (w <= 0 || h <= 0) return 0;
  if (st_set_window(d, x, y, w, h)) return -1;

  uint16_t p = pack565(c);
#ifdef SGFX_RGB565_BYTESWAP
  p = (uint16_t)((p >> 8) | (p << 8));
#endif

  if (d->bus->ops->write_repeat)
    return d->bus->ops->write_repeat(d->bus, &p, sizeof(p), (size_t)w * (size_t)h);

  /* Small buffered fallback */
  enum { CHUNK = 128 };
  uint16_t tmp[CHUNK];
  for (int i = 0; i < CHUNK; ++i) tmp[i] = p;
  size_t total = (size_t)w * (size_t)h;
  while (total){
    size_t n = (total > CHUNK) ? CHUNK : total;
    int rc = st_write_pixels(d, tmp, n, SGFX_FMT_RGB565);
    if (rc) return rc;
    total -= n;
  }
  return 0;
}

/* Optional ops left NULL for now */
const sgfx_driver_ops_t sgfx_st7789_ops = {
  .init         = st_init,
  .reset        = NULL,
  .set_rotation = st_set_rotation,       /* expects (sgfx_device_t*, uint8_t) */
  .set_window   = st_set_window,
  .write_pixels = st_write_pixels,
  .fill_rect    = st_fill_rect,
  .power        = NULL,
  .invert       = NULL,
  .brightness   = NULL,
  .present      = NULL
};

/* Default capabilities (width/height overridden in sgfx_port.h) */
const sgfx_caps_t sgfx_st7789_caps_default = {
  .caps = SGFX_CAP_PARTIAL | SGFX_CAP_HW_FILL
};

#endif
