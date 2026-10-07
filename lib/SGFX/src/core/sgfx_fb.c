#include "sgfx_fb.h"
#include "sgfx_text.h"
#include <stdlib.h>
#include <string.h>

#if defined(ARDUINO) && defined(ESP32) && defined(BOARD_HAS_PSRAM)
  #include <esp_heap_caps.h>
  #ifndef SGFX_FB_ALLOC
    #define SGFX_FB_ALLOC(sz) heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #endif
  #ifndef SGFX_FB_CALLOC
    static inline void* _sgfx_heap_caps_calloc(size_t n, size_t size){
      void* p = heap_caps_malloc(n*size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (p) memset(p, 0, n*size);
      return p;
    }
    #define SGFX_FB_CALLOC(n,sz) _sgfx_heap_caps_calloc((n),(sz))
  #endif
  #define SGFX_FB_FREE(p) heap_caps_free((p))
#else
  #define SGFX_FB_ALLOC(sz)  malloc((sz))
  #define SGFX_FB_CALLOC(n,sz) calloc((n),(sz))
  #define SGFX_FB_FREE(p)    free((p))
#endif

static uint32_t crc32_u8(const uint8_t* p, size_t n){
  uint32_t c=0xFFFFFFFFu;
  for(size_t i=0;i<n;i++){ c ^= p[i];
    for(int k=0;k<8;k++) c = (c>>1) ^ (0xEDB88320u & (-(int)(c&1)));
  }
  return ~c;
}

int sgfx_fb_create(sgfx_fb_t* fb, int w, int h, int tile_w, int tile_h){
  memset(fb,0,sizeof(*fb));
  if (w<=0 || h<=0 || tile_w<=0 || tile_h<=0) return SGFX_ERR_INVAL;
  fb->w=w; fb->h=h; fb->tile_w=tile_w; fb->tile_h=tile_h;
  fb->stride = w * (int)SGFX_BYTESPP;
  size_t sz  = (size_t)w * (size_t)h * (size_t)SGFX_BYTESPP;
  fb->px = (uint8_t*)SGFX_FB_CALLOC(1, sz);
  if(!fb->px) return SGFX_ERR_NOMEM;

  fb->tiles_x = (w + tile_w - 1)/tile_w;
  fb->tiles_y = (h + tile_h - 1)/tile_h;
  size_t tiles = (size_t)fb->tiles_x * fb->tiles_y;
  fb->tile_crc  = (uint32_t*)calloc(tiles, sizeof(uint32_t));
  fb->tile_dirty = (uint8_t*) calloc(tiles, 1);
  if(!fb->tile_crc || !fb->tile_dirty){
    SGFX_FB_FREE(fb->px); free(fb->tile_crc); free(fb->tile_dirty);    memset(fb,0,sizeof(*fb));
    return SGFX_ERR_NOMEM; /* partial failure cleaned */
  }
  return SGFX_OK;
}

void sgfx_fb_destroy(sgfx_fb_t* fb){
  SGFX_FB_FREE(fb->px); free(fb->tile_crc); free(fb->tile_dirty);
  memset(fb,0,sizeof(*fb));
}

void sgfx_fb_mark_dirty_px(sgfx_fb_t* fb, int x,int y,int w,int h){
  if(w<=0||h<=0) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }
  if(x+w>fb->w) w = fb->w - x;
  if(y+h>fb->h) h = fb->h - y;
  if(w<=0||h<=0) return;

  int x0 = x / fb->tile_w, x1 = (x+w-1) / fb->tile_w;
  int y0 = y / fb->tile_h, y1 = (y+h-1) / fb->tile_h;
  for(int ty=y0; ty<=y1; ++ty)
    for(int tx=x0; tx<=x1; ++tx)
      fb->tile_dirty[ty*fb->tiles_x + tx] = 1;
}

void sgfx_fb_rehash_tiles(sgfx_fb_t* fb, int x,int y,int w,int h){
  if(w<=0||h<=0) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }
  if(x+w>fb->w) w = fb->w - x;
  if(y+h>fb->h) h = fb->h - y;
  if(w<=0||h<=0) return;

  int x0 = x / fb->tile_w, x1 = (x+w-1) / fb->tile_w;
  int y0 = y / fb->tile_h, y1 = (y+h-1) / fb->tile_h;
  for(int ty=y0; ty<=y1; ++ty){
    for(int tx=x0; tx<=x1; ++tx){
      int px = tx*fb->tile_w;
      int py = ty*fb->tile_h;
      int tw = (px+fb->tile_w>fb->w)? (fb->w-px): fb->tile_w;
      int th = (py+fb->tile_h>fb->h)? (fb->h-py): fb->tile_h;
      uint8_t* base = (uint8_t*)fb->px + (size_t)py*fb->stride + (size_t)px*SGFX_BYTESPP;
      uint32_t crc = 0;
      for(int j=0;j<th;++j)
        crc ^= crc32_u8(base + (size_t)j*fb->stride, (size_t)tw*SGFX_BYTESPP);
      size_t idx = (size_t)ty*fb->tiles_x + (size_t)tx;
      if (crc != fb->tile_crc[idx]){ fb->tile_crc[idx]=crc; fb->tile_dirty[idx]=1; }
    }
  }
}

void sgfx_ui_fill_norm(sgfx_fb_t* fb, int xpm,int ypm,int wpm,int hpm, sgfx_rgba8_t c){
  int x = sgfx_pm2px(xpm, fb->w);
  int y = sgfx_pm2px(ypm, fb->h);
  int w = sgfx_pm2px(wpm, fb->w);
  int h = sgfx_pm2px(hpm, fb->h);
  if(w<=0||h<=0) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }
  if(x+w>fb->w) w = fb->w - x;
  if(y+h>fb->h) h = fb->h - y;
  if(w<=0||h<=0) return;

  for(int j=0;j<h;++j){
    sgfx_color_t* row = (sgfx_color_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
    for(int i=0;i<w;++i) row[i] = SGFX_PACK(c);
  }
  sgfx_fb_mark_dirty_px(fb, x,y,w,h);
}

/* --- FB Pixel-space helpers (public) --- */
void sgfx_fb_fill_rect_px(sgfx_fb_t* fb, int x, int y, int w, int h, sgfx_rgba8_t c){
  if (!fb || !fb->px) return;
  if (w<=0 || h<=0) return;
  if (x < 0){ w += x; x = 0; }
  if (y < 0){ h += y; y = 0; }
  if (x + w > fb->w) w = fb->w - x;
  if (y + h > fb->h) h = fb->h - y;
  if (w<=0 || h<=0) return;
  for (int j=0; j<h; ++j){
    sgfx_color_t* row = (sgfx_color_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
    for (int i=0; i<w; ++i) row[i] = SGFX_PACK(c);
  }
  sgfx_fb_mark_dirty_px(fb, x, y, w, h);
}

/* --- A8 → FB blend ------------------------------------------------------- */
/* Premultiply alpha using mask and color.a, then blend into FB format.     */
static inline uint8_t u8_mul(uint8_t a, uint8_t b){ return (uint8_t)((a*b + 128) >> 8); }
static inline uint16_t pack565(uint8_t r,uint8_t g,uint8_t b){
  return (uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3));
}
static inline void unpack565(uint16_t c, uint8_t* r,uint8_t* g,uint8_t* b){
  *r = (uint8_t)((c>>8)&0xF8); *r |= *r>>5;
  *g = (uint8_t)((c>>3)&0xFC); *g |= *g>>6;
  *b = (uint8_t)( c     &0x1F); *b = (*b<<3)|(*b>>2);
}

void sgfx_fb_blit_a8(sgfx_fb_t* fb, int x, int y,
                     const uint8_t* a8, int a8_pitch,
                     int w, int h, sgfx_rgba8_t color)
{
  if(!fb || !a8 || w<=0 || h<=0) return;
  /* clip */
  if (x<0){ int d=-x; x=0; w-=d; a8 += d; }
  if (y<0){ int d=-y; y=0; h-=d; a8 += (size_t)d*a8_pitch; }
  if (x+w > fb->w) w = fb->w - x;
  if (y+h > fb->h) h = fb->h - y;
  if (w<=0 || h<=0) return;

  const uint8_t cr = color.r, cg = color.g, cb = color.b, ca = color.a;

#if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
  for(int j=0;j<h;++j){
    uint8_t* dst = (uint8_t*)fb->px + (size_t)(y+j)*fb->stride + (size_t)x*4;
    const uint8_t* src = a8 + (size_t)j*a8_pitch;
    for(int i=0;i<w;++i){
      uint8_t ma = src[i];
      if(!ma){ dst+=4; continue; }
      uint8_t a  = u8_mul(ma, ca);               /* effective alpha */
      uint8_t ia = 255 - a;
      /* dst= (a*color + (1-a)*dst) */
      dst[0] = (uint8_t)((a*cb + ia*dst[0] + 127)/255);
      dst[1] = (uint8_t)((a*cg + ia*dst[1] + 127)/255);
      dst[2] = (uint8_t)((a*cr + ia*dst[2] + 127)/255);
      dst[3] = (uint8_t)((a + ia*dst[3] + 127)/255);
      dst+=4;
    }
  }
#else /* RGB565 FB */
  for(int j=0;j<h;++j){
    uint16_t* dst = (uint16_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
    const uint8_t* src = a8 + (size_t)j*a8_pitch;
    for(int i=0;i<w;++i){
      uint8_t ma = src[i];
      if(!ma){ dst++; continue; }
      uint8_t a  = u8_mul(ma, ca);
      if (a == 255) { *dst++ = pack565(cr, cg, cb); continue; }
      uint8_t ia = 255 - a;
      uint8_t dr,dg,db; unpack565(dst[0], &dr,&dg,&db);
      uint8_t r = (uint8_t)((a*cr + ia*dr + 127)/255);
      uint8_t g = (uint8_t)((a*cg + ia*dg + 127)/255);
      uint8_t b = (uint8_t)((a*cb + ia*db + 127)/255);
      dst[0] = pack565(r,g,b);
      dst++;
    }
  }
#endif
  sgfx_fb_mark_dirty_px(fb, x,y,w,h);
}