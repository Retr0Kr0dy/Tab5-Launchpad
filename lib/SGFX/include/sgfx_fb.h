#pragma once
#include "sgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Pixel format selection (default: RGB565) ----------------------- */
#if !defined(SGFX_COLOR_RGB565) && !defined(SGFX_COLOR_RGBA8888)
#  define SGFX_COLOR_RGB565 1
#endif

#if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
  typedef sgfx_rgba8_t  sgfx_color_t;
# define SGFX_BYTESPP   4
# define SGFX_PACK(c)   (c)
#else
  typedef uint16_t      sgfx_color_t;
# define SGFX_BYTESPP   2
  static inline sgfx_color_t SGFX_PACK(sgfx_rgba8_t c){
    return (uint16_t)(((c.r & 0xF8)<<8) | ((c.g & 0xFC)<<3) | (c.b>>3));
  }
#endif

typedef struct {
  int w,h;               /* in pixels */
  int stride;            /* in bytes: w * SGFX_BYTESPP */
  uint8_t* px;           /* pixel buffer (format = sgfx_color_t) */
  int tile_w, tile_h;
  int tiles_x, tiles_y;
  uint32_t* tile_crc;
  uint8_t*  tile_dirty;
} sgfx_fb_t;

int  sgfx_fb_create(sgfx_fb_t* fb, int w, int h, int tile_w, int tile_h);
void sgfx_fb_destroy(sgfx_fb_t* fb);
void sgfx_fb_mark_dirty_px(sgfx_fb_t* fb, int x, int y, int w, int h);
void sgfx_fb_rehash_tiles(sgfx_fb_t* fb, int x, int y, int w, int h);
static inline int sgfx_pm2px(int pm, int size_px){ return (pm * size_px + 500) / 1000; }
void sgfx_ui_fill_norm(sgfx_fb_t* fb, int xpm,int ypm,int wpm,int hpm, sgfx_rgba8_t c);

// Pixel-space helpers (draw into RGBA8888 framebuffer)
void sgfx_fb_fill_rect_px(sgfx_fb_t* fb, int x, int y, int w, int h, sgfx_rgba8_t c);

typedef struct {
  uint16_t* linebuf;
  int       linebuf_px;
} sgfx_present_t;


// Optional presenter statistics (counts per present_frame call)
typedef struct {
  uint32_t frames;       // total frames presented
  uint32_t rects_pushed; // CASET/RASET regions sent (coalesced rects)
  uint32_t pixels_sent;  // total pixels converted/sent
  uint32_t bytes_sent;   // total bytes over the bus (approx; RGB565=2*px)
  uint32_t tiles_dirty;  // tiles marked dirty this frame
} sgfx_present_stats_t;

void sgfx_present_stats_reset(sgfx_present_stats_t* s);

int  sgfx_present_init(sgfx_present_t* pr, int max_line_px);
void sgfx_present_deinit(sgfx_present_t* pr);
int  sgfx_present_frame(sgfx_present_t* pr, sgfx_device_t* dev, sgfx_fb_t* fb);

/* Alpha8 → colored blend into FB (RGB565 or RGBA8888) */
void sgfx_fb_blit_a8(sgfx_fb_t* fb,
                     int x, int y,
                     const uint8_t* a8, int a8_pitch,
                     int w, int h,
                     sgfx_rgba8_t color);

#ifdef __cplusplus
}
#endif
