#include "sgfx_fb.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Default to RGB565 framebuffer unless RGBA8888 explicitly selected */
#if !defined(SGFX_COLOR_RGB565) && !defined(SGFX_COLOR_RGBA8888)
#  define SGFX_COLOR_RGB565 1
#endif

static inline uint16_t rgb565_of(sgfx_rgba8_t c){
  return (uint16_t)(((c.r & 0xF8)<<8) | ((c.g & 0xFC)<<3) | (c.b>>3));
}

int sgfx_present_init(sgfx_present_t* pr, int max_line_px){
  memset(pr,0,sizeof(*pr));
  if (max_line_px <= 0) return SGFX_ERR_INVAL;
  pr->linebuf = (uint16_t*)malloc((size_t)max_line_px * sizeof(uint16_t));
  if(!pr->linebuf) return SGFX_ERR_NOMEM;
  pr->linebuf_px = max_line_px;
  return SGFX_OK;
}

void sgfx_present_deinit(sgfx_present_t* pr){
  free(pr->linebuf); memset(pr,0,sizeof(*pr));
}

static void push_rect(sgfx_present_t* pr, sgfx_device_t* d,
                      sgfx_fb_t* fb, int x,int y,int w,int h){
  int maxw = pr->linebuf_px;
  d->drv->set_window(d, x,y,w,h);
  for(int j=0;j<h;++j){
    #if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
      /* RGBA8888 framebuffer: convert each chunk into linebuf (RGB565) */
      const sgfx_rgba8_t* src = (const sgfx_rgba8_t*)((const uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
      int remaining = w, col = 0;
      while (remaining > 0){
        int chunk = remaining > maxw ? maxw : remaining;
        for(int i=0;i<chunk;++i) pr->linebuf[i] = rgb565_of(src[col+i]);
        d->drv->write_pixels(d, pr->linebuf, (size_t)chunk, SGFX_FMT_RGB565);
        remaining -= chunk;
        col += chunk;
      }
    #else
      /* RGB565 framebuffer: push rows directly (no conversion) */
      const uint16_t* src565 = (const uint16_t*)((const uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
      int remaining = w, col = 0;
      while (remaining > 0){
        int chunk = remaining > maxw ? maxw : remaining;
        d->drv->write_pixels(d, src565 + col, (size_t)chunk, SGFX_FMT_RGB565);
        remaining -= chunk;
        col += chunk;
      }
    #endif
  }
}

/* Direct-framebuffer path (MIPI-DSI & friends): the panel owns a hardware
 * scanout buffer that the display peripheral DMAs out on its own, so instead
 * of streaming rows over a command bus we just copy the dirty rect into it and
 * tell the driver to cache-sync that region.
 *
 * Pixel format is assumed to match between the software sgfx_fb_t and the
 * hardware buffer (both RGB565 in the expected case; RGBA8888 if SGFX is built
 * with SGFX_COLOR_RGBA8888 and the panel was configured to match). No format
 * conversion is attempted here.
 */
static void copy_rect_to_fb(uint8_t* hw, size_t hw_stride,
                            sgfx_fb_t* fb, int x, int y, int w, int h){
  const size_t bpp        = (size_t)SGFX_BYTESPP;
  const size_t row_bytes  = (size_t)w * bpp;
  for (int j = 0; j < h; ++j){
    const uint8_t* src = (const uint8_t*)fb->px + (size_t)(y+j)*(size_t)fb->stride + (size_t)x*bpp;
    uint8_t*       dst = hw + (size_t)(y+j)*hw_stride + (size_t)x*bpp;
    memcpy(dst, src, row_bytes);
  }
}

static sgfx_present_stats_t g_sgfx_stats;
void sgfx_present_stats_reset(sgfx_present_stats_t* s){
  if (!s) return;
  s->frames = s->rects_pushed = s->pixels_sent = s->bytes_sent = s->tiles_dirty = 0;
}
int sgfx_present_frame(sgfx_present_t* pr, sgfx_device_t* d, sgfx_fb_t* fb){
  if (!d || !d->drv) return SGFX_ERR_NOSUP;

  /* Path selection, resolved once per frame.
   *  - direct   : get_fb_ptr (+ optional flush_surface) — DSI-style panels;
   *               preferred whenever the panel exposes a scanout buffer.
   *  - streamed : set_window + write_pixels (SPI/I2C, MIPI-DBI panels)
   * The hardware buffer can't move mid-frame, so it is queried exactly once.
   */
  uint8_t* hw_fb     = NULL;
  size_t   hw_stride = 0;
  if (d->drv->get_fb_ptr){
    hw_fb = (uint8_t*)d->drv->get_fb_ptr(d, &hw_stride);
    if (hw_fb && hw_stride == 0) hw_stride = (size_t)fb->w * (size_t)SGFX_BYTESPP;
  }
  if (!hw_fb && !(d->drv->set_window && d->drv->write_pixels)) return SGFX_ERR_NOSUP;

  const int TX = fb->tiles_x, TY = fb->tiles_y;
  const int TW = fb->tile_w,  TH = fb->tile_h;

  g_sgfx_stats.frames++;
for(int ty=0; ty<TY; ++ty){
    int tx=0;
    while(tx<TX){
      while(tx<TX && !fb->tile_dirty[ty*TX+tx]) tx++;
      if(tx>=TX) break;
      int run_start = tx;
      while(tx<TX && fb->tile_dirty[ty*TX+tx]) tx++;
      int run_end = tx-1;

      int x = run_start*TW;
      int y = ty*TH;
      int w = (run_end - run_start + 1)*TW;
      if (x+w > fb->w) w = fb->w - x;
      int h = TH; if (y+h > fb->h) h = fb->h - y;

      // stats: tiles in this horizontal run
for(int k=run_start; k<=run_end; ++k) g_sgfx_stats.tiles_dirty++;
g_sgfx_stats.rects_pushed++;
g_sgfx_stats.pixels_sent += (uint32_t)w * (uint32_t)h;
g_sgfx_stats.bytes_sent  += (uint32_t)w * (uint32_t)h * 2u;
if (hw_fb){
  copy_rect_to_fb(hw_fb, hw_stride, fb, x,y,w,h);
  if (d->drv->flush_surface) d->drv->flush_surface(d, x,y,w,h);
} else {
  push_rect(pr, d, fb, x,y,w,h);
}

      for(int k=run_start; k<=run_end; ++k) fb->tile_dirty[ty*TX+k]=0;
    }
  }
  return SGFX_OK;
}
