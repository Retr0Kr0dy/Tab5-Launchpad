// SGFX Universal Feature Demo (RGB565 default + new text API)
// - Small but readable text (SM=3, MD=4, LG~6.5)
// - Top/Bottom anchored text helpers to avoid clipping at borders
// - Single SCENE_PAUSE_MS knob for scene pacing
// - Uses SDF when available; falls back to 5x7 bitmap on FB

#ifndef SGFX_DEMO_DOUBLE_FB
#define SGFX_DEMO_DOUBLE_FB 0
#endif

#ifndef SCENE_PAUSE_MS
#define SCENE_PAUSE_MS 2000  // <— adjust one knob for all scene delays
#endif

#include <cstdio>
#include <cstdint>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #define SGFX_DELAY(ms) delay(ms)
  #define SGFX_MILLIS()  millis()
#else
  #include <time.h>
  static uint32_t SGFX_MILLIS(){
    static uint64_t t0 = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec*1000ULL + (uint64_t)ts.tv_nsec/1000000ULL;
    if (!t0) t0 = now;
    return (uint32_t)(now - t0);
  }
  static void SGFX_DELAY(uint32_t ms){
    struct timespec ts = { (time_t)(ms/1000), (long)((ms%1000)*1000000L) };
    nanosleep(&ts, NULL);
  }
#endif

#include "sgfx.h"
#include "sgfx_fb.h"
#include "sgfx_port.h"
#include "sgfx_text.h"
#include "sgfx_font_builtin.h"   // 5x7 fallback table

#ifndef SGFX_SCRATCH_BYTES
  #define SGFX_SCRATCH_BYTES 4096
#endif
static uint8_t scratch[SGFX_SCRATCH_BYTES];

/* ========= Color helpers ========= */
static inline sgfx_rgba8_t RGBA(uint8_t r,uint8_t g,uint8_t b,uint8_t a){ return (sgfx_rgba8_t){r,g,b,a}; }
static inline sgfx_rgba8_t WHITE(){ return RGBA(255,255,255,255); }
static inline sgfx_rgba8_t BLACK(){ return RGBA(0,0,0,255); }
static inline sgfx_rgba8_t GRAY(uint8_t v){ return RGBA(v,v,v,255); }

#if !defined(SGFX_COLOR_RGB565) && !defined(SGFX_COLOR_RGBA8888)
#  define SGFX_COLOR_RGB565 1   // default FB storage is RGB565
#endif

// Pack/unpack when touching FB memory directly
static inline uint16_t pack565_rgba(sgfx_rgba8_t c){
  return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}
static inline sgfx_rgba8_t unpack565(uint16_t v){
  uint8_t r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
  uint8_t g = (uint8_t)(((v >>  5) & 0x3F) * 255 / 63);
  uint8_t b = (uint8_t)(( v        & 0x1F) * 255 / 31);
  return RGBA(r,g,b,255);
}
static inline sgfx_rgba8_t RANDCOL(){
  uint8_t r = (uint8_t)(SGFX_MILLIS() * 17u);
  uint8_t g = (uint8_t)(SGFX_MILLIS() * 29u);
  uint8_t b = (uint8_t)(SGFX_MILLIS() * 43u);
  return RGBA(r,g,b,255);
}

/* ========= Small logical text sizes (slightly bigger than before) ========= */
#define TEXT_PX_SM   3.0f   // labels, HUD, captions
#define TEXT_PX_MD   4.0f   // subtitle
#define TEXT_PX_LG   6.5f   // compact title

/* ---------- FB helpers ---------- */
static inline void fb_mark_dirty_all(sgfx_fb_t* fb){
  sgfx_fb_mark_dirty_px(fb, 0,0, fb->w, fb->h);
}
static inline void fb_full_clear(sgfx_fb_t* fb, sgfx_rgba8_t c){
  if (!fb || !fb->px) return;
  #if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
    for (int y=0;y<fb->h;++y){
      sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)y*fb->stride);
      for (int x=0;x<fb->w;++x) row[x] = c;
    }
  #else
    uint16_t v = pack565_rgba(c);
    for (int y=0;y<fb->h;++y){
      uint16_t* row = (uint16_t*)((uint8_t*)fb->px + (size_t)y*fb->stride);
      for (int x=0;x<fb->w;++x) row[x] = v;
    }
  #endif
  fb_mark_dirty_all(fb);
}
static inline void fb_put_px(sgfx_fb_t* fb, int x,int y, sgfx_rgba8_t c){
  if ((unsigned)x >= (unsigned)fb->w || (unsigned)y >= (unsigned)fb->h) return;
  #if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
    ((sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)y*fb->stride))[x] = c;
  #else
    ((uint16_t*)((uint8_t*)fb->px + (size_t)y*fb->stride))[x] = pack565_rgba(c);
  #endif
  sgfx_fb_mark_dirty_px(fb, x,y,1,1);
}
static inline void fb_draw_fast_hline(sgfx_fb_t* fb, int x,int y,int w, sgfx_rgba8_t c){
  if (y<0 || y>=fb->h) return;
  if (x<0){ w += x; x = 0; }
  if (x+w > fb->w) w = fb->w - x;
  if (w<=0) return;
  #if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
    sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)y*fb->stride) + x;
    for (int i=0;i<w;++i) row[i] = c;
  #else
    uint16_t* row = (uint16_t*)((uint8_t*)fb->px + (size_t)y*fb->stride) + x;
    uint16_t v = pack565_rgba(c);
    for (int i=0;i<w;++i) row[i] = v;
  #endif
  sgfx_fb_mark_dirty_px(fb, x,y,w,1);
}
static inline void fb_draw_fast_vline(sgfx_fb_t* fb, int x,int y,int h, sgfx_rgba8_t c){
  if (x<0 || x>=fb->w) return;
  if (y<0){ h += y; y = 0; }
  if (y+h > fb->h) h = fb->h - y;
  if (h<=0) return;
  #if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
    for (int j=0;j<h;++j){
      ((sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride))[x] = c;
    }
  #else
    uint16_t v = pack565_rgba(c);
    for (int j=0;j<h;++j){
      ((uint16_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride))[x] = v;
    }
  #endif
  sgfx_fb_mark_dirty_px(fb, x,y,1,h);
}
static inline void fb_fill_rect(sgfx_fb_t* fb, int x,int y,int w,int h, sgfx_rgba8_t c){
  if (w<=0||h<=0) return;
  if (x<0){ w += x; x = 0; }
  if (y<0){ h += y; y = 0; }
  if (x+w > fb->w) w = fb->w - x;
  if (y+h > fb->h) h = fb->h - y;
  if (w<=0||h<=0) return;
  #if defined(SGFX_COLOR_RGBA8888) && SGFX_COLOR_RGBA8888
    for (int j=0;j<h;++j){
      sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
      for (int i=0;i<w;++i) row[i] = c;
    }
  #else
    uint16_t v = pack565_rgba(c);
    for (int j=0;j<h;++j){
      uint16_t* row = (uint16_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
      for (int i=0;i<w;++i) row[i] = v;
    }
  #endif
  sgfx_fb_mark_dirty_px(fb, x,y,w,h);
}
static inline void fb_draw_rect(sgfx_fb_t* fb, int x,int y,int w,int h, sgfx_rgba8_t c){
  if (w<=1 || h<=1){ if (w>0 && h>0) fb_put_px(fb, x,y,c); return; }
  fb_draw_fast_hline(fb, x,y, w, c);
  fb_draw_fast_hline(fb, x,y+h-1, w, c);
  fb_draw_fast_vline(fb, x,y, h, c);
  fb_draw_fast_vline(fb, x+w-1,y, h, c);
}

/* ========= Device + font ========= */
static sgfx_device_t dev;
static sgfx_font_t*  g_font = nullptr;
static inline bool sgfx_ready(){ return (dev.drv != nullptr) && (dev.caps.width>0) && (dev.caps.height>0); }

/* ========= Text helpers (SDF when available; 5x7 fallback on FB) ========= */

// 5x7 onto FB (top-left anchor)
static void fb_text5x7_draw_line(sgfx_fb_t* fb, int x, int y,
                                 const char* s, int sx, int sy, sgfx_rgba8_t col)
{
  if (!fb || !s || sx<=0 || sy<=0) return;
  for (; *s; ++s, x += 6*sx){
    unsigned char ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) continue;
    uint8_t cols[5];
    if (!sgfx_font5x7_get((char)ch, cols)) continue;
    for (int i=0;i<5;++i){
      uint8_t bits = cols[i];   // bit 0..6 = row 0..6 (top..bottom)
      for (int row=0; row<7; ++row){
        if (bits & (uint8_t)(1u<<row)){
          fb_fill_rect(fb, x + i*sx, y + row*sy, sx, sy, col);
        }
      }
    }
  }
}

static inline int fb5x7_scale_from_px(float px){
  return (px <= 7.f) ? 1 : (px < 13.f ? 2 : (px < 20.f ? 3 : 4));
}
static inline int cstrlen(const char* s){ int n=0; if(!s) return 0; while(s[n]) ++n; return n; }

static void measure_text_line(const char* s, float px, int* out_adv, int* out_h){
  if (g_font){
    sgfx_text_style_t st = sgfx_text_style_default(WHITE(), px);
    sgfx_text_metrics_t mt = {0};
    sgfx_text_measure_line(s, g_font, &st, &mt);
    if (out_adv) *out_adv = mt.advance;
    if (out_h)   *out_h   = mt.bbox_h;
  } else {
    int sy = fb5x7_scale_from_px(px); if (sy < 1) sy = 1;
    int adv = cstrlen(s) * 6 * sy; // 5 columns + 1 space
    int hh  = 7 * sy;
    if (out_adv) *out_adv = adv;
    if (out_h)   *out_h   = hh;
  }
}

// Baseline draw (SDF baseline, 5x7 emulates baseline)
static void text_draw_line(sgfx_fb_t* fb, int x, int y, const char* s, float px, sgfx_rgba8_t col){
  if (g_font){
    sgfx_text_style_t st = sgfx_text_style_default(col, px);
    sgfx_text_draw_line(fb, x, y, s, g_font, &st);
  } else {
    int sy = fb5x7_scale_from_px(px); if (sy < 1) sy = 1;
    int sx = sy;
    fb_text5x7_draw_line(fb, x, y - 6*sy, s, sx, sy, col); // emulate baseline
  }
}

// Top-left anchor: draw so the text's top aligns to y_top for both SDF and 5x7
static void text_draw_top_left(sgfx_fb_t* fb, int x, int y_top, const char* s, float px, sgfx_rgba8_t col){
  if (g_font){
    // approximate baseline from top: ascent ≈ 0.85*px works well for tiny sizes
    int baseline = y_top + (int)(px * 0.85f + 0.5f);
    text_draw_line(fb, x, baseline, s, px, col);
  } else {
    int sy = fb5x7_scale_from_px(px); if (sy < 1) sy = 1;
    fb_text5x7_draw_line(fb, x, y_top, s, sy, sy, col);
  }
}

// Bottom-left anchor: place so the text sits above y_bottom
static void text_draw_bottom_left(sgfx_fb_t* fb, int x, int y_bottom, const char* s, float px, sgfx_rgba8_t col){
  int adv=0, hh=0; measure_text_line(s, px, &adv, &hh);
  int y_top = y_bottom - hh;
  if (y_top < 0) y_top = 0;
  text_draw_top_left(fb, x, y_top, s, px, col);
}

// Center-top anchor
static void text_draw_center_top(sgfx_fb_t* fb, int y_top, const char* s, float px, sgfx_rgba8_t col){
  int adv=0, hh=0; measure_text_line(s, px, &adv, &hh);
  int x = (fb->w - adv)/2;
  text_draw_top_left(fb, x, y_top, s, px, col);
}

/* ========= Utility ========= */
static void corner_marks_fb(sgfx_fb_t* fb, int inset, sgfx_rgba8_t c){
  const int W = fb->w, H = fb->h;
  fb_draw_fast_hline(fb, inset, inset, 8, c);
  fb_draw_fast_vline(fb, inset, inset, 8, c);
  fb_draw_fast_hline(fb, W-8-inset, inset, 8, c);
  fb_draw_fast_vline(fb, W-1-inset, inset, 8, c);
  fb_draw_fast_hline(fb, inset, H-1-inset, 8, c);
  fb_draw_fast_vline(fb, inset, H-8-inset, 8, c);
  fb_draw_fast_hline(fb, W-8-inset, H-1-inset, 8, c);
  fb_draw_fast_vline(fb, W-1-inset, H-8-inset, 8, c);
}

static inline int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }

static void checker_fb(sgfx_fb_t* fb, int x,int y,int w,int h,int cell){
  for (int j=0;j<h;j+=cell){
    for (int i=0;i<w;i+=cell){
      bool on = ((i/cell) ^ (j/cell)) & 1;
      fb_fill_rect(fb, x+i, y+j, (int)clampi(cell,1,w-i), (int)clampi(cell,1,h-j), on?WHITE():BLACK());
    }
  }
}

/* ========= BLIT builders ========= */
static void mono1_build_sprite(uint8_t* buf, int W, int H){
  memset(buf, 0x00, (size_t)W*(H/8));
  auto P = [&](int x,int y){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    int page = y >> 3;
    int bit  = y & 7;
    buf[page*W + x] |= (uint8_t)(1u << bit);
  };
  const int w=32, h=16;
  for (int x=0;x<w;x++){ P(x,0); P(x,h-1); }
  for (int y=0;y<h;y++){ P(0,y); P(w-1,y); }
  for (int i=0;i<((w<h)?w:h);++i){ P(i,i); P(w-1-i,i); }
  for (int y=2;y<h-2;y+=2){
    for (int x=2;x<w-2;x+=4) P(x,y);
  }
}
static void rgb565_build_sprite(uint16_t* buf, int W, int H){
  for (int y=0;y<H;++y){
    for (int x=0;x<W;++x){
      bool on = ((x>>2) ^ (y>>2)) & 1;
      sgfx_rgba8_t c = on ? RGBA(255,200,30,255) : RGBA(30,120,255,255);
      buf[y*W + x] = pack565_rgba(c);
    }
  }
}

static int fb_blit_mono1(sgfx_fb_t* fb, int x,int y, int W,int H, const uint8_t* mono, size_t stride_bytes){
  for (int j=0;j<H;++j){
    const uint8_t* row = mono + (size_t)j*stride_bytes;
    for (int i=0;i<W;++i){
      uint8_t byte = row[i/8];
      bool on = (byte >> (i&7)) & 1u;
      fb_put_px(fb, x+i, y+j, on ? WHITE() : BLACK());
    }
  }
  sgfx_fb_mark_dirty_px(fb, x,y,W,H);
  return SGFX_OK;
}
static int fb_blit_rgb565(sgfx_fb_t* fb, int x,int y, int W,int H, const uint16_t* rgb, size_t stride_bytes){
  for (int j=0;j<H;++j){
    const uint16_t* row = (const uint16_t*)((const uint8_t*)rgb + (size_t)j*stride_bytes);
    for (int i=0;i<W;++i){
      fb_put_px(fb, x+i, y+j, unpack565(row[i]));
    }
  }
  sgfx_fb_mark_dirty_px(fb, x,y,W,H);
  return SGFX_OK;
}
static int try_blit_any_fb(sgfx_fb_t* fb, int x,int y){
  const int W = 32, H = 16;
#if !defined(SGFX_DEMO_FORCE_RGB565)
  if (SGFX_SCRATCH_BYTES >= (size_t)(W*(H/8))){
    uint8_t* mono = (uint8_t*)scratch;
    mono1_build_sprite(mono, W, H);
    int rc = fb_blit_mono1(fb, x, y, W, H, mono, (size_t)W);
    if (rc == SGFX_OK) return SGFX_OK;
  }
#endif
#if !defined(SGFX_DEMO_FORCE_MONO1)
  if (SGFX_SCRATCH_BYTES >= (size_t)(W*H*2)){
    uint16_t* rgb = (uint16_t*)scratch;
    rgb565_build_sprite(rgb, W, H);
    int rc = fb_blit_rgb565(fb, x, y, W, H, rgb, (size_t)(W*2));
    if (rc == SGFX_OK) return SGFX_OK;
    return rc;
  }
#endif
  return SGFX_ERR_NOMEM;
}

/* ========= Scenes ========= */
static void scene_intro(uint8_t boot_rot){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  sgfx_set_rotation(&dev, boot_rot);
  fb_full_clear(&fb, BLACK());

  float title_px = 10.f;
  float sub_px   = 8.f;

  text_draw_top_left(&fb, 2, 2, "SGFX", title_px, WHITE());
  text_draw_top_left(&fb, 2, 2 + (int)title_px + 3, "UNIVERSAL DEMO", sub_px, WHITE());

  char line[48];
  snprintf(line, sizeof line, "%dx%d ROT=%u", W, H, (unsigned)boot_rot);
  text_draw_bottom_left(&fb, 2, H-2, line, TEXT_PX_SM, WHITE());

  corner_marks_fb(&fb, 0, WHITE());

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

static void scene_addressing(){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  fb_full_clear(&fb, BLACK());
  fb_draw_rect(&fb, 0,0, W,H, WHITE());
  fb_draw_fast_hline(&fb, 0, H/2, W, WHITE());
  fb_draw_fast_vline(&fb, W/2, 0, H, WHITE());
  corner_marks_fb(&fb, 2, WHITE());
  text_draw_top_left(&fb, 2, 2, "ADDRESSING", TEXT_PX_SM, WHITE());

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

static void scene_clipping(){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  fb_full_clear(&fb, BLACK());
  text_draw_top_left(&fb, 2, 2, "CLIPPING", TEXT_PX_SM, WHITE());

  sgfx_rect_t win = { (int16_t)(W/6), (int16_t)(H/6), (int16_t)(2*W/3), (int16_t)(2*H/3) };
  fb_draw_rect(&fb, win.x, win.y, win.w, win.h, WHITE());
  checker_fb(&fb, win.x+1, win.y+1, win.w-2, win.h-2, 6);

  // inside window labels (safe)
  text_draw_top_left(&fb, win.x + 2, win.y + 2, "LEFT CLIP", TEXT_PX_SM, WHITE());
  {
    const char* s = "BTM CLIP";
    int adv=0, hh=0; measure_text_line(s, TEXT_PX_SM, &adv, &hh);
    int x = win.x + win.w - adv - 2;
    text_draw_bottom_left(&fb, x, win.y + win.h - 2, s, TEXT_PX_SM, WHITE());
  }

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

static void scene_rotation_sweep(uint8_t boot_rot){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  sgfx_present_init(&pr, W);
  for (uint8_t r=0; r<4; ++r){
    sgfx_set_rotation(&dev, r);
    fb_full_clear(&fb, BLACK());
    char buf[32];
    snprintf(buf, sizeof buf, "ROT=%u", (unsigned)r);
    text_draw_top_left(&fb, 2, 2, buf, TEXT_PX_MD, WHITE());
    corner_marks_fb(&fb, 0, WHITE());
    fb_draw_rect(&fb, 10, 10, W-20, H-20, WHITE());
    sgfx_present_frame(&pr, &dev, &fb);
    SGFX_DELAY(SCENE_PAUSE_MS/2);
  }
  sgfx_present_deinit(&pr);
  sgfx_set_rotation(&dev, boot_rot);
  sgfx_fb_destroy(&fb);
}

static void scene_text_scaling(){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  fb_full_clear(&fb, BLACK());
  text_draw_top_left(&fb, 2, 2, "TEXT SCALING", TEXT_PX_SM, WHITE());
  int y = 2 + (int)TEXT_PX_SM + 6;

  text_draw_top_left(&fb, 2, y, "SDF px=3", 3.f, WHITE()); y += 10;
  text_draw_top_left(&fb, 2, y, "SDF px=4", 4.f, WHITE()); y += 12;
  text_draw_top_left(&fb, 2, y, "SDF px=5", 5.f, WHITE()); y += 14;

  text_draw_top_left(&fb, 2, y, "BOLD + ITALIC", TEXT_PX_MD, WHITE());
  {
    sgfx_text_style_t st = sgfx_text_style_default(WHITE(), TEXT_PX_MD);
    st.bold_px = 0.6f; st.italic_skew = 0.2f;
    if (g_font){
      // place directly under the label
      int y2 = y + (int)TEXT_PX_MD + 2;
      sgfx_text_draw_line(&fb, 2, y2 + (int)(TEXT_PX_MD*0.85f + 0.5f), "AaBbCc 012345", g_font, &st);
    } else {
      text_draw_top_left(&fb, 2, y + (int)TEXT_PX_MD + 2, "AaBbCc 012345", TEXT_PX_MD, WHITE());
    }
    y += 30;
  }

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

static void scene_blit(){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  fb_full_clear(&fb, BLACK());
  text_draw_top_left(&fb, 2, 2, "BLIT DEMO (auto fmt)", TEXT_PX_SM, WHITE());

  int rc1 = try_blit_any_fb(&fb, 8, 16);
  int rc2 = try_blit_any_fb(&fb, 8+40, 16+6);
  int rc3 = try_blit_any_fb(&fb, 8+80, 16+12);

  char line[48];
  snprintf(line, sizeof line, "rc=%d,%d,%d", rc1, rc2, rc3);
  text_draw_bottom_left(&fb, 2, H-2, line, TEXT_PX_SM, WHITE());

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

static void scene_color_or_mono_fill(){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  fb_full_clear(&fb, BLACK());
  text_draw_top_left(&fb, 2, 2, "FILLS / GRADIENT", TEXT_PX_SM, WHITE());

  for (int x=0; x<W; ++x){
    uint8_t v = (uint8_t)((x * 255) / (W>1?W-1:1));
    fb_fill_rect(&fb, x, 12, 1, H/3, RGBA(v, (uint8_t)(255-v), (uint8_t)(v/2), 255));
  }
  int y = 12 + H/3 + 2;
  for (int i=0;i<6;++i){
    fb_fill_rect(&fb, 2 + i*(W-4)/6, y, (W-8)/6, H - y - 2, RANDCOL());
  }

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

static void scene_perf_fills(){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  fb_full_clear(&fb, BLACK());
  text_draw_top_left(&fb, 2, 2, "PERF: checker fills", TEXT_PX_SM, WHITE());
  const int frames = 30;
  uint32_t t0 = SGFX_MILLIS();
  for (int f=0; f<frames; ++f){
    checker_fb(&fb, 0, 10, W, H-10, (f&1)?8:4);
  }
  uint32_t t1 = SGFX_MILLIS();
  float avg_ms = (t1 - t0) / (float)frames;

  char buf[48];
  snprintf(buf, sizeof buf, "AVG=%.1f ms", avg_ms);
  fb_full_clear(&fb, BLACK());
  text_draw_center_top(&fb, 2, "PERF RESULT", TEXT_PX_MD, WHITE());
  text_draw_top_left(&fb, 2, 2 + (int)TEXT_PX_MD + 4, buf, TEXT_PX_SM, WHITE());

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

static void scene_overlap_merge(){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t fb; sgfx_present_t pr;
  if (sgfx_fb_create(&fb, W, H, 16,16) != SGFX_OK || fb.px == NULL) return;

  fb_full_clear(&fb, BLACK());
  text_draw_top_left(&fb, 2, 2, "OVERLAP MERGE", TEXT_PX_SM, WHITE());
  for (int y=10; y<42; y+=3) fb_fill_rect(&fb, 8, y, W-16, 1, WHITE());
  for (int x=8; x<W-8; x+=7)  fb_fill_rect(&fb, x, 9, 1, 36, WHITE());

  sgfx_present_init(&pr, W);
  sgfx_present_frame(&pr, &dev, &fb);
  sgfx_present_deinit(&pr);
  sgfx_fb_destroy(&fb);

  SGFX_DELAY(SCENE_PAUSE_MS);
}

/* ========= Arduino-ish entry ========= */
void setup(){
  if (sgfx_autoinit(&dev, scratch, sizeof scratch)){
    while(1){ SGFX_DELAY(1000); } // failed
  }
  g_font = sgfx_font_open_builtin(); // may be NULL (stub) — fallback will kick in
  SGFX_DELAY(300);

  const uint8_t boot_rot = (uint8_t)(SGFX_ROT & 3);
  sgfx_set_rotation(&dev, boot_rot);

  scene_intro(boot_rot);
  scene_addressing();
  scene_clipping();
  scene_rotation_sweep(boot_rot);
  scene_text_scaling();
  scene_blit();
  scene_color_or_mono_fill();
  scene_perf_fills();
  scene_overlap_merge();

  // Final
  {
    const int W = dev.caps.width, H = dev.caps.height;
    sgfx_fb_t fb; sgfx_present_t pr;
    if (sgfx_fb_create(&fb, W, H, 16,16) == SGFX_OK && fb.px){
      fb_full_clear(&fb, BLACK());
      text_draw_top_left(&fb, 2, 2, "DEMO COMPLETE", TEXT_PX_SM, WHITE());
      text_draw_top_left(&fb, 2, 2 + (int)(TEXT_PX_SM*2) + 4, "PRESS RESET", TEXT_PX_SM, WHITE());
      sgfx_present_init(&pr, W);
      sgfx_present_frame(&pr, &dev, &fb);
      sgfx_present_deinit(&pr);
      sgfx_fb_destroy(&fb);
    }
  }
}

void loop(){
  static bool g_lazy_init_attempted = false;
  if (!sgfx_ready() && !g_lazy_init_attempted){
    static uint16_t scratch16[4096];
    (void)sgfx_autoinit(&dev, scratch16, sizeof scratch16);
    g_lazy_init_attempted = true;
  }
  if (!sgfx_ready()){ SGFX_DELAY(10); return; }

  static bool fb_inited = false;
  static sgfx_fb_t fb_main;
  #if SGFX_DEMO_DOUBLE_FB
  static sgfx_fb_t fb_anim;
  #endif
  static sgfx_present_t pr;
  static bool nofb_mode = false;

  struct RectPM { int xpm, ypm, wpm, hpm; int vxpm, vypm; sgfx_rgba8_t c; };
  static RectPM R[5];
  static int W=0,H=0;

  static int px[5] = {0}, py[5] = {0}, pw[5] = {0}, ph[5] = {0};
  static bool first_frame = true;

  if (!fb_inited){
    W = (int)dev.caps.width;
    H = (int)dev.caps.height;

    int rcA = sgfx_fb_create(&fb_main, W, H, 16, 16);
    #if SGFX_DEMO_DOUBLE_FB
    int rcB = sgfx_fb_create(&fb_anim, W, H, 16, 16);
    #endif
    #if SGFX_DEMO_DOUBLE_FB
    if (rcA != SGFX_OK || rcB != SGFX_OK || fb_main.px == NULL || fb_anim.px == NULL){
    #else
    if (rcA != SGFX_OK || fb_main.px == NULL){
    #endif
      nofb_mode = true;
    } else {
      sgfx_present_init(&pr, W);
      bool fb_supported = dev.drv && dev.drv->set_window && dev.drv->write_pixels;
      if (!fb_supported) { nofb_mode = true; }
    }
    for (int i=0;i<5;++i){
      R[i].wpm = 80 + i*20;  R[i].hpm = 60 + i*15;
      R[i].xpm = (i*150) % 700; R[i].ypm = (i*230) % 700;
      R[i].vxpm = (i&1)? 13 : -11; R[i].vypm = (i&2)? 9 : -7;
      R[i].c = (sgfx_rgba8_t){ (uint8_t)(40+i*40), (uint8_t)(200 - i*30), (uint8_t)(80 + i*30), 255 };
    }
    fb_inited = true;
  }

  if (nofb_mode){
    sgfx_clear(&dev, BLACK());
    for (int i=0;i<5;++i){
      RectPM& r = R[i];
      r.xpm += r.vxpm; r.ypm += r.vypm;
      if (r.xpm < 0 || r.xpm + r.wpm >= 1000){ r.vxpm = -r.vxpm; r.xpm += r.vxpm; }
      if (r.ypm < 0 || r.ypm + r.hpm >= 1000){ r.vypm = -r.vypm; r.ypm += r.vypm; }
      int x = (r.xpm * dev.caps.width  + 500)/1000;
      int y = (r.ypm * dev.caps.height + 500)/1000;
      int w = (r.wpm * dev.caps.width  + 500)/1000;
      int h = (r.hpm * dev.caps.height + 500)/1000;
      sgfx_fill_rect(&dev, x,y,w,h, r.c);
    }
    if (dev.drv && dev.drv->present){ dev.drv->present(&dev); }
    SGFX_DELAY(6);
    return;
  }

  sgfx_fb_t* tgt = &fb_main;
  if (first_frame){
    fb_full_clear(tgt, BLACK());
    first_frame = false;
  }

  for (int i=0;i<5;++i){
    RectPM& r = R[i];
    if (pw[i] > 0 && ph[i] > 0){
      fb_fill_rect(tgt, px[i], py[i], pw[i], ph[i], BLACK());
    }
    r.xpm += r.vxpm; r.ypm += r.vypm;
    if (r.xpm < 0 || r.xpm + r.wpm >= 1000){ r.vxpm = -r.vxpm; r.xpm += r.vxpm; }
    if (r.ypm < 0 || r.ypm + r.hpm >= 1000){ r.vypm = -r.vypm; r.ypm += r.vypm; }
    int x = (r.xpm * W + 500)/1000;
    int y = (r.ypm * H + 500)/1000;
    int w = (r.wpm * W + 500)/1000;
    int h = (r.hpm * H + 500)/1000;
    fb_fill_rect(tgt, x,y,w,h, r.c);
    px[i]=x; py[i]=y; pw[i]=w; ph[i]=h;
  }

  // FPS overlay (small, top-left)
  static uint32_t t_prev = 0;
  static uint32_t acc_ms = 0;
  static int      acc_frames = 0;
  static float    fps_display = 0.0f;
  uint32_t t_now = SGFX_MILLIS();
  if (t_prev==0) t_prev = t_now;
  uint32_t dt_ms = (t_now - t_prev);
  t_prev = t_now;
  acc_ms += dt_ms;
  acc_frames += 1;
  if (acc_ms >= 500){
    if (acc_ms > 0) fps_display = (1000.0f * (float)acc_frames) / (float)acc_ms;
    acc_ms = 0; acc_frames = 0;
  }

  {
    char buf[8];
    int v = (int)(fps_display + 0.5f); if (v < 0) v = 0; if (v > 999) v = 999;
    if (v < 100) snprintf(buf, sizeof buf, "%02d", v); else snprintf(buf, sizeof buf, "%03d", v);

    float px = TEXT_PX_SM;
    int adv=0, hh=0; measure_text_line(buf, px, &adv, &hh);
    int pad = 2, bx = 2, by = 2;
    fb_fill_rect(tgt, bx, by, adv + 2*pad, hh + 2*pad, BLACK());
    text_draw_top_left(tgt, bx + pad, by + pad, buf, px, WHITE());
  }

  (void)sgfx_present_frame(&pr, &dev, &fb_main);
  SGFX_DELAY(0);
}
