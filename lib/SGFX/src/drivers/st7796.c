#ifdef SGFX_DRV_ST7796

#include "sgfx.h"
#include "st77xx_common.h"
#include "sgfx_port.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

// forward declaration to satisfy ops initializer
static int st_invert(sgfx_device_t* d, bool on);


/* ==== Driver flag mapping (make per-driver flags usable as defaults) ==== */
#ifdef SGFX_ST7796_BGR
#  undef  SGFX_DEFAULT_BGR_ORDER
#  define SGFX_DEFAULT_BGR_ORDER SGFX_ST7796_BGR
#endif
#ifdef SGFX_ST7796_INVERT
#  undef  SGFX_DEFAULT_INVERT
#  define SGFX_DEFAULT_INVERT SGFX_ST7796_INVERT
#endif

#ifndef SGFX_PANEL_W
#  ifdef SGFX_W
#    define SGFX_PANEL_W SGFX_W
#  else
#    define SGFX_PANEL_W 320
#  endif
#endif
#ifndef SGFX_PANEL_H
#  ifdef SGFX_H
#    define SGFX_PANEL_H SGFX_H
#  else
#    define SGFX_PANEL_H 480
#  endif
#endif

/* ========== ST77xx Commands (subset used) ========== */
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

#ifndef SGFX_DEFAULT_INVERT
#  define SGFX_DEFAULT_INVERT 0
#endif


/* ========== Offsets (alias common macros) ========== */
#ifndef SGFX_ST77XX_XOFF
# ifdef SGFX_ST7796_COLSTART
#  define SGFX_ST77XX_XOFF SGFX_ST7796_COLSTART
# else
#  define SGFX_ST77XX_XOFF 0
# endif
#endif
#ifndef SGFX_ST77XX_YOFF
# ifdef SGFX_ST7796_ROWSTART
#  define SGFX_ST77XX_YOFF SGFX_ST7796_ROWSTART
# else
#  define SGFX_ST77XX_YOFF 0
# endif
#endif

/* ========== Private state ========== */
typedef struct {
  uint16_t xoff_portrait;
  uint16_t yoff_portrait;
  uint8_t  rotation;
  uint8_t  bgr;
} st_priv_t;

/* ========== Small helpers ========== */
static int st_send(sgfx_device_t* d, uint8_t cmd, const void* data, size_t n){
  if (d->bus->ops->write_cmd(d->bus, cmd)) return -1;
  if (n && data) return d->bus->ops->write_data(d->bus, data, n);
  return 0;
}
static inline uint16_t pack565(sgfx_rgba8_t c){
  return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

/* ========== Rotation/offset handling ========== */
static void st_offsets_for(st_priv_t* p, uint8_t rot, uint16_t* xo, uint16_t* yo){
  if ((rot & 1) == 0) { *xo = p->xoff_portrait; *yo = p->yoff_portrait; }
  else { *xo = p->yoff_portrait; *yo = p->xoff_portrait; }
}

/* ========== Driver ops ========== */
static int st_init(sgfx_device_t* d){
  st_priv_t* p = (st_priv_t*)calloc(1, sizeof(*p));
  if (!p) return SGFX_ERR_NOMEM;
  d->scratch = p;

  p->xoff_portrait = (uint16_t)SGFX_ST77XX_XOFF;
  p->yoff_portrait = (uint16_t)SGFX_ST77XX_YOFF;
  p->bgr = (uint8_t)(
  #ifdef SGFX_DEFAULT_BGR_ORDER
    SGFX_DEFAULT_BGR_ORDER
  #else
    0
  #endif
  );

  if (st_send(d, ST77_SWRESET, NULL, 0)) return -1;
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 5);
  if (st_send(d, ST77_SLPOUT, NULL, 0)) return -1;
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 120);

  { uint8_t v = 0x55; if (st_send(d, ST77_COLMOD, &v, 1)) return -1; } // 16-bit
  uint8_t mad = st77xx_madctl_for(0, p->bgr);
  if (st_send(d, ST77_MADCTL, &mad, 1)) return -1;

  if (SGFX_DEFAULT_INVERT) { if (st_send(d, ST77_INVON, NULL, 0)) return -1; }
  else { if (st_send(d, ST77_INVOFF, NULL, 0)) return -1; }
  if (st_send(d, ST77_DISPON, NULL, 0)) return -1;
  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);
  return 0;
}


static int st_set_rotation(sgfx_device_t* d, uint8_t rot){
  st_priv_t* p = (st_priv_t*)d->scratch;
  p->rotation = (rot & 3);
  uint8_t mad = st77xx_madctl_for(p->rotation, p->bgr);
  /* Optional extra mirroring */
#ifdef SGFX_ST7796_MIRROR_Y
  if (SGFX_ST7796_MIRROR_Y) mad |= MADCTL_MY;
#endif
#ifdef SGFX_ST7796_MIRROR_X
  if (SGFX_ST7796_MIRROR_X) mad |= MADCTL_MX;
#endif
  return st_send(d, ST77_MADCTL, &mad, 1);
}
static int st_set_window(sgfx_device_t* d, int x, int y, int w, int h){
  st_priv_t* p = (st_priv_t*)d->scratch;
  uint16_t xo, yo; st_offsets_for(p, p->rotation, &xo, &yo);
  uint16_t x0 = (uint16_t)(x + xo);
  uint16_t y0 = (uint16_t)(y + yo);
  uint16_t x1 = (uint16_t)(x + w - 1 + xo);
  uint16_t y1 = (uint16_t)(y + h - 1 + yo);

  uint8_t buf[4];
  buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)(x0 & 0xFF);
  buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)(x1 & 0xFF);
  if (st_send(d, ST77_CASET, buf, 4)) return -1;

  buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)(y0 & 0xFF);
  buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)(y1 & 0xFF);
  if (st_send(d, ST77_RASET, buf, 4)) return -1;

  return st_send(d, ST77_RAMWR, NULL, 0);
}

static int st_write_pixels(sgfx_device_t* d, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
  if (src_fmt == SGFX_FMT_RGB565) {
#ifdef SGFX_RGB565_BYTESWAP
    // Swap bytes to send high-byte first (panel expects big-endian 565).
    // Use a reusable heap buffer to avoid large stack usage and stack overflows.
#ifndef SGFX_SPI_SWAP_BUF_BYTES
#define SGFX_SPI_SWAP_BUF_BYTES 4096
#endif
    static uint8_t* swap_buf = NULL;
    static size_t   swap_cap = 0;

    size_t need = SGFX_SPI_SWAP_BUF_BYTES;
    if (swap_cap < need){
      uint8_t* nb = (uint8_t*)realloc(swap_buf, need);
      if (!nb) return SGFX_ERR_NOMEM;
      swap_buf = nb; swap_cap = need;
    }

    const uint8_t* s = (const uint8_t*)px;
    size_t remaining = count;               // pixels
    while (remaining) {
      size_t npx = remaining;
      size_t maxpx = (swap_cap / 2);
      if (npx > maxpx) npx = maxpx;

      // swap npx pixels into swap_buf
      for (size_t i = 0; i < npx; ++i) {
        swap_buf[2*i]   = s[2*i+1];  // high byte first
        swap_buf[2*i+1] = s[2*i];    // then low byte
      }

      int r = d->bus->ops->write_data(d->bus, swap_buf, npx * 2);
      if (r) return r;
      s += npx * 2;
      remaining -= npx;
    }
    return SGFX_OK;
#else
    // No byteswap: forward to bus optimized path if available, otherwise raw bytes
    if (d->bus->ops->write_pixels)
      return d->bus->ops->write_pixels(d->bus, px, count, src_fmt);
    return d->bus->ops->write_data(d->bus, px, count * 2);
#endif
  }
  // Non-565 formats: treat 'count' as bytes
  return d->bus->ops->write_data(d->bus, px, count);
}

static int st_fill_rect(sgfx_device_t* d, int x, int y, int w, int h, sgfx_rgba8_t c){
  if (w <= 0 || h <= 0) return 0;
  if (st_set_window(d, x, y, w, h)) return -1;

  uint16_t p = pack565(c);
  if (d->bus->ops->write_repeat)
    return d->bus->ops->write_repeat(d->bus, &p, sizeof(p), (size_t)w * (size_t)h);

  enum { CHUNK = 128 };
  uint16_t tmp[CHUNK];
  for (int i=0;i<CHUNK;++i) tmp[i]=p;
  size_t left = (size_t)w * (size_t)h;
  while (left){
    size_t n = left > CHUNK ? CHUNK : left;
    int rc = d->bus->ops->write_data(d->bus, tmp, n*2);
    if (rc) return rc;
    left -= n;
  }
  return 0;
}

static int st_present(sgfx_device_t* d){
  (void)d; return 0; // no-op for now
}

static const sgfx_caps_t st_caps = {
  .width  = SGFX_PANEL_W,
  .height = SGFX_PANEL_H,
};

const sgfx_caps_t sgfx_st7796_caps = { .width = SGFX_PANEL_W, .height = SGFX_PANEL_H, .native_fmt = SGFX_FMT_RGB565, .bpp = 16, .caps = 0 };

const sgfx_driver_ops_t sgfx_st7796_ops = {
  .init         = st_init,
  .set_window   = st_set_window,
  .write_pixels = st_write_pixels,
  .fill_rect    = st_fill_rect,
  .present      = st_present,
  .set_rotation = st_set_rotation,
  .power        = NULL,
  .invert       = st_invert,
  .brightness   = NULL,
};

// runtime invert support
static int st_invert(sgfx_device_t* d, bool on){
    const uint8_t cmd = on ? ST77_INVON : ST77_INVOFF;
    return st_send(d, cmd, NULL, 0);
}

#endif /* SGFX_DRV_ST7796 */
