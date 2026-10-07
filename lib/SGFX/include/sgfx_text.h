#pragma once
#include "sgfx_fb.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SGFX_FONT_BITMAP_A8 = 1,  /* grayscale atlas (alpha8)        */
  SGFX_FONT_SDF_A8    = 2   /* signed distance field (alpha8)  */
} sgfx_font_kind_t;

typedef struct sgfx_font sgfx_font_t;

typedef struct {
  float px;            /* target pixel size (height) */
  float letter_spacing;
  float line_gap_px;   /* extra gap added to font line gap */
  sgfx_rgba8_t color;
  /* style transforms (applied at sample time for SDF; bitmap uses AA scale) */
  float bold_px;       /* ≥0: positive grows strokes (dilation)           */
  float outline_px;    /* ≥0: draw outline; fill kept if fill_alpha>0     */
  float italic_skew;   /* tangent of skew angle (e.g., 0.25 ≈ 14°)        */
  uint8_t fill_alpha;  /* 0..255 multiply to color.a for fill             */
  uint8_t outline_alpha; /* alpha for outline color                        */
  sgfx_rgba8_t outline_color;
  /* shadow */
  int shadow_dx, shadow_dy;
  uint8_t shadow_alpha; /* multiplied with color.a for shadow */
} sgfx_text_style_t;

typedef struct {
  int ascent, descent, line_gap; /* at style.px, in pixels (rounded) */
  int advance;                    /* total advance of measured string */
  int bbox_w, bbox_h;             /* tight box for the run */
} sgfx_text_metrics_t;

/* --- Font lifecycle ------------------------------------------------------- */
sgfx_font_t* sgfx_font_open_builtin(void); /* small SDF, embedded */
void         sgfx_font_close(sgfx_font_t* f);
sgfx_font_kind_t sgfx_font_kind(const sgfx_font_t* f);

/* External loaders (A8 bitmap or A8 SDF packed in SGFXF v1 binary) */
sgfx_font_t* sgfx_font_load_from_memory(const void* data, size_t size);
typedef size_t (*sgfx_stream_read_fn)(void* user, void* dst, size_t len);
typedef int    (*sgfx_stream_seek_fn)(void* user, long off, int whence);
sgfx_font_t* sgfx_font_load_from_stream(sgfx_stream_read_fn r, sgfx_stream_seek_fn s, void* user);

/* --- Draw / Measure ------------------------------------------------------- */
void sgfx_text_draw_line(sgfx_fb_t* fb, int x, int y,
                         const char* utf8, const sgfx_font_t* font,
                         const sgfx_text_style_t* style);
void sgfx_text_measure_line(const char* utf8, const sgfx_font_t* font,
                            const sgfx_text_style_t* style,
                            sgfx_text_metrics_t* out);

/* Convenience defaults */
static inline sgfx_text_style_t sgfx_text_style_default(sgfx_rgba8_t color, float px){
  sgfx_text_style_t s = {
    .px = px, .letter_spacing = 0.f, .line_gap_px = 0.f,
    .color = color, .bold_px = 0.f, .outline_px = 0.f,
    .italic_skew = 0.f, .fill_alpha = 255, .outline_alpha = 255,
    .outline_color = {0,0,0,255}, .shadow_dx = 0, .shadow_dy = 0, .shadow_alpha = 0
  };
  return s;
}

#ifdef __cplusplus
}
#endif
