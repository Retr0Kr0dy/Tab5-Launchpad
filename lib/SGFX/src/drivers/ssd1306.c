#ifdef SGFX_DRV_SSD1306

#include "sgfx.h"
#include <string.h>
#include <stdlib.h>

/* ---- SSD1306 commands ---- */
#define SSD1306_SETCONTRAST         0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_NORMALDISPLAY       0xA6
#define SSD1306_DISPLAYOFF          0xAE
#define SSD1306_DISPLAYON           0xAF
#define SSD1306_MULTIPLEX           0xA8
#define SSD1306_SETDISPLAYOFFSET    0xD3
#define SSD1306_SETSTARTLINE        0x40
#define SSD1306_MEMORYMODE          0x20  /* 0:horiz 1:vert 2:page */
#define SSD1306_COLUMNADDR          0x21
#define SSD1306_PAGEADDR            0x22
#define SSD1306_SEGREMAP            0xA0  /* |1 to mirror columns */
#define SSD1306_COMSCANINC          0xC0
#define SSD1306_COMSCANDEC          0xC8
#define SSD1306_SETDISPLAYCLOCKDIV  0xD5
#define SSD1306_CHARGEPUMP          0x8D
#define SSD1306_SETCOMPINS          0xDA
#define SSD1306_SETPRECHARGE        0xD9
#define SSD1306_SETVCOMDETECT       0xDB
#define SSD1306_DEACTIVATESCROLL    0x2E

#ifndef SGFX_SH110X_COL_OFFSET
#define SGFX_SH110X_COL_OFFSET 0
#endif

/* ---- bus helpers ---- */
static inline int ssd_cmd(sgfx_device_t* d, uint8_t c){ return d->bus->ops->write_cmd(d->bus, c); }
static inline int ssd_data(sgfx_device_t* d, const void* p, size_t n){ return d->bus->ops->write_data(d->bus, p, n); }

/* ---- driver state (single device) ---- */
static struct {
  uint8_t* fb;              /* 1bpp, pages stacked: width*(height/8) */
  int fb_bytes;
  int w, h, pages;
  /* buffered window state for compat write_pixels path */
  int win_x, win_y, win_w, win_h;
  int cur_col, cur_row;
} s;

/* ---- init ---- */
static int ssd_init(sgfx_device_t* d){
  s.w = d->caps.width;
  s.h = d->caps.height;
  s.pages = s.h / 8;
  s.fb_bytes = s.w * s.pages;

  /* allocate framebuffer (use scratch if big enough, else malloc) */
  if ((int)d->scratch_bytes >= s.fb_bytes) {
    s.fb = (uint8_t*)d->scratch;
  } else {
    s.fb = (uint8_t*)malloc((size_t)s.fb_bytes);
    if (!s.fb) return SGFX_ERR_NOMEM;
  }
  memset(s.fb, 0x00, (size_t)s.fb_bytes);

  /* clean, deterministic init */
  ssd_cmd(d, SSD1306_DISPLAYOFF);
  ssd_cmd(d, SSD1306_SETDISPLAYCLOCKDIV); ssd_cmd(d, 0x80);
  ssd_cmd(d, SSD1306_MULTIPLEX);           ssd_cmd(d, (uint8_t)(s.h-1));
  ssd_cmd(d, SSD1306_SETDISPLAYOFFSET);    ssd_cmd(d, 0x00);
  ssd_cmd(d, SSD1306_SETSTARTLINE | 0x00);

#ifdef SGFX_OLED_EXTVCC
  ssd_cmd(d, SSD1306_CHARGEPUMP); ssd_cmd(d, 0x10);
#else
  ssd_cmd(d, SSD1306_CHARGEPUMP); ssd_cmd(d, 0x14);
#endif

  /* PAGE addressing */
  ssd_cmd(d, SSD1306_MEMORYMODE);          ssd_cmd(d, 0x02);

  /* Default orientation; rotation will change this */
  ssd_cmd(d, (uint8_t)(SSD1306_SEGREMAP | 0x01));
  ssd_cmd(d, SSD1306_COMSCANDEC);

  ssd_cmd(d, SSD1306_SETCOMPINS);         ssd_cmd(d, (s.h==32) ? 0x02 : 0x12);
  ssd_cmd(d, SSD1306_SETCONTRAST);        ssd_cmd(d, (s.h==32) ? 0x8F : 0xCF);
  ssd_cmd(d, SSD1306_SETPRECHARGE);       ssd_cmd(d, 0xF1);
  ssd_cmd(d, SSD1306_SETVCOMDETECT);      ssd_cmd(d, 0x40);
  ssd_cmd(d, SSD1306_DISPLAYALLON_RESUME);
  ssd_cmd(d, SSD1306_NORMALDISPLAY);
  ssd_cmd(d, SSD1306_DEACTIVATESCROLL);
  ssd_cmd(d, SSD1306_DISPLAYON);

  if (d->bus->ops->delay_ms) d->bus->ops->delay_ms(d->bus, 10);

  /* clear panel once to match fb */
  /* full window and stream fb zeros */
  ssd_cmd(d, SSD1306_COLUMNADDR);
  ssd_cmd(d, (uint8_t)0 + SGFX_SH110X_COL_OFFSET);
  ssd_cmd(d, (uint8_t)(s.w-1) + SGFX_SH110X_COL_OFFSET);
  ssd_cmd(d, SSD1306_PAGEADDR);
  ssd_cmd(d, 0);
  ssd_cmd(d, (uint8_t)(s.pages-1));
  /* send in chunks */
  const size_t CH = 32;
  for (int off = 0; off < s.fb_bytes; off += (int)CH){
    size_t n = (size_t)((off + (int)CH <= s.fb_bytes) ? CH : (s.fb_bytes - off));
    ssd_data(d, s.fb + off, n);
  }
  return SGFX_OK;
}

/* ---- rotation flips SEG/COM ---- */
static int ssd_set_rotation(sgfx_device_t* d, uint8_t rot){
  switch (rot & 3){
    case 0: ssd_cmd(d, SSD1306_SEGREMAP | 0x01); ssd_cmd(d, SSD1306_COMSCANDEC); break;
    case 1: ssd_cmd(d, SSD1306_SEGREMAP | 0x01); ssd_cmd(d, SSD1306_COMSCANINC); break;
    case 2: ssd_cmd(d, SSD1306_SEGREMAP | 0x00); ssd_cmd(d, SSD1306_COMSCANINC); break;
    case 3: ssd_cmd(d, SSD1306_SEGREMAP | 0x00); ssd_cmd(d, SSD1306_COMSCANDEC); break;
  }
  /* critical: reset start line to 0 after changing COM/SEG */
  ssd_cmd(d, SSD1306_SETSTARTLINE | 0x00);
  return SGFX_OK;
}

/* ---- set window for a single page band ---- */
static int ssd_set_window(sgfx_device_t* d, int x0, int page, int x1){
  if (x0 < 0) x0 = 0;
  if (x1 > s.w - 1) x1 = s.w - 1;
  if (page < 0) page = 0;
  if (page > s.pages - 1) page = s.pages - 1;

  int rc = ssd_cmd(d, SSD1306_COLUMNADDR); if (rc) return rc;
  rc = ssd_cmd(d, (uint8_t)x0 + SGFX_SH110X_COL_OFFSET); if (rc) return rc;
  rc = ssd_cmd(d, (uint8_t)x1 + SGFX_SH110X_COL_OFFSET); if (rc) return rc;

  rc = ssd_cmd(d, SSD1306_PAGEADDR); if (rc) return rc;
  rc = ssd_cmd(d, (uint8_t)page);    if (rc) return rc;
  rc = ssd_cmd(d, (uint8_t)page);    if (rc) return rc;
  return SGFX_OK;
}

/* ---- fb helpers ---- */
static inline uint8_t* fb_byte(int x, int y){
  /* page-major: byte index = page*s.w + x; bit = y & 7 */
  return &s.fb[(y>>3)*s.w + x];
}

/* ---- MONO fill rect with proper bitwise merge ---- */
static int ssd_fill_rect(sgfx_device_t* d, int x,int y,int w,int h, sgfx_rgba8_t c){
  /* clip */
  int x0 = x, y0 = y;
  int x1 = x + w - 1;
  int y1 = y + h - 1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= s.w)  x1 = s.w - 1;
  if (y1 >= s.h)  y1 = s.h - 1;
  if (x1 < x0 || y1 < y0) return SGFX_OK;

  /* on = any nonzero RGB -> set bits; black -> clear bits */
  const int set_on = (c.r | c.g | c.b) != 0;

  const int page_start = y0 >> 3;
  const int page_end   = y1 >> 3;

  for (int page = page_start; page <= page_end; ++page){
    const int band_y0 = page << 3;
    const int band_y1 = band_y0 + 7;

    /* build a mask for the vertical span that lies within this page band */
    uint8_t mask = 0;
    const int from = (y0 > band_y0) ? (y0 - band_y0) : 0;
    const int to   = (y1 < band_y1) ? (y1 - band_y0) : 7;
    for (int b = from; b <= to; ++b) mask |= (uint8_t)(1u << b);

    /* per column, merge bits in fb, then stream changed bytes */
    /* we’ll reuse a small temp buffer on the stack per band */
    int cols = x1 - x0 + 1;
    if (cols <= 0) continue;
    /* mutate fb in place */
    for (int xcol = x0; xcol <= x1; ++xcol){
      uint8_t* p = &s.fb[page*s.w + xcol];
      uint8_t prev = *p;
      uint8_t next = set_on ? (prev | mask) : (prev & (uint8_t)~mask);
      if (next != prev) *p = next;
    }

    /* push the band sub-rectangle from fb to panel */
    int rc = ssd_set_window(d, x0, page, x1);
    if (rc) return rc;

    /* stream columns straight from fb (already merged) */
    const uint8_t* row = &s.fb[page*s.w + x0];
    rc = ssd_data(d, row, (size_t)cols);
    if (rc) return rc;
  }
  return SGFX_OK;
}

/* full present: flush entire framebuffer (optional) */
static int ssd_present(sgfx_device_t* d){
  int rc = ssd_cmd(d, SSD1306_COLUMNADDR); if (rc) return rc;
  rc = ssd_cmd(d, (uint8_t)0 + SGFX_SH110X_COL_OFFSET); if (rc) return rc;
  rc = ssd_cmd(d, (uint8_t)(s.w-1) + SGFX_SH110X_COL_OFFSET); if (rc) return rc;
  rc = ssd_cmd(d, SSD1306_PAGEADDR); if (rc) return rc;
  rc = ssd_cmd(d, 0); if (rc) return rc;
  rc = ssd_cmd(d, (uint8_t)(s.pages-1)); if (rc) return rc;

  /* stream the whole fb */
  const size_t CH = 64;
  for (int off = 0; off < s.fb_bytes; off += (int)CH){
    size_t n = (size_t)((off + (int)CH <= s.fb_bytes) ? CH : (s.fb_bytes - off));
    rc = ssd_data(d, s.fb + off, n);
    if (rc) return rc;
  }
  return SGFX_OK;
}


/* ---- compat: rectangular set_window + RGB565 write_pixels for SGFX FB ---- */
/* We don't program the panel window here; we'll patch s.fb per row then stream that page */
static int ssd_set_window_rect(sgfx_device_t* d, int x,int y,int w,int h){
  (void)d;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > s.w) w = s.w - x;
  if (y + h > s.h) h = s.h - y;
  s.win_x = x; s.win_y = y; s.win_w = w; s.win_h = h;
  s.cur_col = 0; s.cur_row = 0;
  return SGFX_OK;
}

/* Convert a single RGB565 pixel to mono bit (1=on) */
static inline int mono_from_rgb565(uint16_t p){
  /* fastest: any non-black -> on */
  return p != 0;
}

/* Stream of RGB565 words comes row-by-row; we merge into s.fb then push that page band */
static int ssd_write_pixels_rect(sgfx_device_t* d, const void* src, size_t count, sgfx_pixfmt_t fmt){
  if (fmt != SGFX_FMT_RGB565) return SGFX_ERR_NOSUP;
  const uint16_t* px = (const uint16_t*)src;
  size_t i = 0;
  while (i < count){
    int x = s.win_x + s.cur_col;
    int y = s.win_y + s.cur_row;
    if (x < 0 || x >= s.w || y < 0 || y >= s.h) {
      /* skip but keep advancing to avoid infinite loop */
    } else {
      uint8_t* fbbyte = &s.fb[(y>>3)*s.w + x];
      uint8_t mask = (uint8_t)(1u << (y & 7));
      if (mono_from_rgb565(px[i])) *fbbyte |=  mask;
      else                         *fbbyte &= (uint8_t)~mask;
    }
    s.cur_col++;
    i++;
    if (s.cur_col >= s.win_w){
      /* end of row: push this row's page bytes */
      int page = (s.win_y + s.cur_row) >> 3;
      int x0 = s.win_x;
      int x1 = s.win_x + s.win_w - 1;
      int rc = ssd_set_window(d, x0, page, x1);
      if (rc) return rc;
      const uint8_t* row = &s.fb[page*s.w + x0];
      rc = ssd_data(d, row, (size_t)(x1 - x0 + 1));
      if (rc) return rc;
      s.cur_col = 0;
      s.cur_row++;
      if (s.cur_row >= s.win_h){
        /* rectangle complete */
        s.cur_row = s.cur_col = 0;
        break;
      }
    }
  }
  return SGFX_OK;
}
/* ---- driver ops ---- */
const sgfx_driver_ops_t sgfx_ssd1306_ops = {
  .init = ssd_init,
  .reset = NULL,
  .set_rotation = ssd_set_rotation,
  .set_window = ssd_set_window_rect,          /* compat for SGFX present */
  .write_pixels = ssd_write_pixels_rect,
  .fill_rect = ssd_fill_rect,  /* everything funnels here */
  .power = NULL,
  .invert = NULL,
  .brightness = NULL,
  .present = ssd_present
};

/* advertise caps */
const sgfx_caps_t sgfx_ssd1306_caps_128x64 = {
  .width = 128, .height = 64,
  .native_fmt = SGFX_FMT_MONO1, .bpp = 1,
  .caps = SGFX_CAP_PARTIAL
};

#endif