// gfx_core.c — SGFX core (clean)
// - RGBA8 is the public color type.
// - Generic paths stream RGB565 (2 B/px) through the driver for speed.
// - No legacy text here. Use sgfx_text.* (preferred) or the optional helper in sgfx_font_builtin.*.

#include "sgfx.h"
#include <string.h>
#include <stdint.h>

/* -------------------- tiny helpers -------------------- */

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline uint16_t pack565(sgfx_rgba8_t c) {
  return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

/* -------------------- core init / state -------------------- */

int sgfx_init(sgfx_device_t* dev, sgfx_bus_t* bus,
              const sgfx_driver_ops_t* drv, const sgfx_caps_t* caps,
              void* scratch_buf, size_t scratch_len)
{
  if (!dev || !bus || !drv || !caps) return SGFX_ERR_INVAL;
  memset(dev, 0, sizeof *dev);
  dev->bus = bus;
  dev->drv = drv;
  dev->caps = *caps;
  dev->scratch = scratch_buf;
  dev->scratch_bytes = scratch_len;
  dev->clip = (sgfx_rect_t){0,0,(int16_t)caps->width,(int16_t)caps->height};

  if (bus->ops && bus->ops->begin) bus->ops->begin(bus);

  /* default mono palette */
  dev->palette.size = 2;
  dev->palette.colors[0] = (sgfx_rgba8_t){0,0,0,255};
  dev->palette.colors[1] = (sgfx_rgba8_t){255,255,255,255};

  if (dev->drv->init) return dev->drv->init(dev);
  return SGFX_OK;
}

void sgfx_set_clip(sgfx_device_t* d, sgfx_rect_t r){
  int16_t x = (int16_t)clampi(r.x, 0, d->caps.width);
  int16_t y = (int16_t)clampi(r.y, 0, d->caps.height);
  int16_t w = (int16_t)clampi(r.w, 0, d->caps.width  - x);
  int16_t h = (int16_t)clampi(r.h, 0, d->caps.height - y);
  d->clip = (sgfx_rect_t){x,y,w,h};
}
void sgfx_reset_clip(sgfx_device_t* d){
  d->clip = (sgfx_rect_t){0,0,(int16_t)d->caps.width,(int16_t)d->caps.height};
}

void sgfx_set_rotation(sgfx_device_t* d, uint8_t rot){
  d->rotation = (uint8_t)(rot & 3);
  if (d->drv->set_rotation) d->drv->set_rotation(d, d->rotation);
}

void sgfx_set_palette(sgfx_device_t* d, const sgfx_palette_t* pal){
  if (pal) d->palette = *pal;
}
void sgfx_set_dither(sgfx_device_t* d, uint8_t mode){ d->dither = mode; }

/* -------------------- primitives -------------------- */

int sgfx_clear(sgfx_device_t* d, sgfx_rgba8_t color){
  return sgfx_fill_rect(d, 0, 0, d->caps.width, d->caps.height, color);
}

int sgfx_draw_pixel(sgfx_device_t* d, int x, int y, sgfx_rgba8_t c){
  if (x < d->clip.x || y < d->clip.y ||
      x >= (d->clip.x + d->clip.w) || y >= (d->clip.y + d->clip.h))
    return SGFX_OK;
  return sgfx_fill_rect(d, x, y, 1, 1, c);
}

int sgfx_fill_rect(sgfx_device_t* d, int x, int y, int w, int h, sgfx_rgba8_t c){
  /* clip to current window */
  int x0 = clampi(x,       d->clip.x,               d->clip.x + d->clip.w);
  int y0 = clampi(y,       d->clip.y,               d->clip.y + d->clip.h);
  int x1 = clampi(x + w,   d->clip.x,               d->clip.x + d->clip.w);
  int y1 = clampi(y + h,   d->clip.y,               d->clip.y + d->clip.h);
  if (x1 <= x0 || y1 <= y0) return SGFX_OK;

  /* driver fast path if available */
  if (d->drv->fill_rect) return d->drv->fill_rect(d, x0, y0, x1 - x0, y1 - y0, c);

  /* generic path: set window then stream RGB565 tiles from scratch */
  if (!d->drv->set_window || !d->drv->write_pixels) return SGFX_ERR_NOSUP;
  int rc = d->drv->set_window(d, x0, y0, x1 - x0, y1 - y0);
  if (rc) return rc;

  size_t count  = (size_t)((x1 - x0) * (y1 - y0));
  size_t max_px = d->scratch_bytes / 2; /* 16-bit temp buffer */
  if (max_px == 0) return SGFX_ERR_NOMEM;

  uint16_t* buf = (uint16_t*)d->scratch;
  uint16_t  v16 = pack565(c);

  while (count){
    size_t n = count > max_px ? max_px : count;
    for (size_t i = 0; i < n; ++i) buf[i] = v16;
    rc = d->drv->write_pixels(d, buf, n, SGFX_FMT_RGB565);
    if (rc) return rc;
    count -= n;
  }
  return SGFX_OK;
}

int sgfx_draw_fast_hline(sgfx_device_t* d, int x,int y,int w, sgfx_rgba8_t c){
  return sgfx_fill_rect(d, x, y, w, 1, c);
}
int sgfx_draw_fast_vline(sgfx_device_t* d, int x,int y,int h, sgfx_rgba8_t c){
  return sgfx_fill_rect(d, x, y, 1, h, c);
}
int sgfx_draw_rect(sgfx_device_t* d, int x,int y,int w,int h, sgfx_rgba8_t c){
  if (w <= 0 || h <= 0) return SGFX_OK;
  int rc = 0;
  rc |= sgfx_draw_fast_hline(d, x,       y,       w, c);
  rc |= sgfx_draw_fast_hline(d, x,       y + h-1, w, c);
  rc |= sgfx_draw_fast_vline(d, x,       y,       h, c);
  rc |= sgfx_draw_fast_vline(d, x + w-1, y,       h, c);
  return rc;
}

int sgfx_present(sgfx_device_t* d){
  if (d->drv->present) return d->drv->present(d);
  return SGFX_OK;
}

/* Basic blit: streams MONO1 or RGB565 to driver; driver converts to panel-native */
int sgfx_blit(sgfx_device_t* d, int x,int y, int w,int h,
              sgfx_pixfmt_t src_fmt, const void* pixels, size_t pitch_bytes)
{
  if (!d->drv->set_window || !d->drv->write_pixels) return SGFX_ERR_NOSUP;
  if (x >= d->clip.x + d->clip.w || y >= d->clip.y + d->clip.h) return SGFX_OK;

  int rx = x < d->clip.x ? d->clip.x - x : 0;
  int ry = y < d->clip.y ? d->clip.y - y : 0;
  int rw = (x + w) > (d->clip.x + d->clip.w) ? (d->clip.x + d->clip.w) - x : w;
  int rh = (y + h) > (d->clip.y + d->clip.h) ? (d->clip.y + d->clip.h) - y : h;
  if (rw <= 0 || rh <= 0) return SGFX_OK;

  int rc = d->drv->set_window(d, x + rx, y + ry, rw, rh);
  if (rc) return rc;

  const uint8_t* row = (const uint8_t*)pixels + (size_t)ry * pitch_bytes
                     + ((src_fmt == SGFX_FMT_RGB565) ? (size_t)rx * 2u : (size_t)rx /* MONO1 assumes byte-aligned x */);

  for (int j = 0; j < rh; ++j){
    size_t count = (size_t)rw; /* driver interprets by src_fmt */
    rc = d->drv->write_pixels(d, row, count, src_fmt);
    if (rc) return rc;
    row += pitch_bytes;
  }
  return SGFX_OK;
}

/* End of file */
