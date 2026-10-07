// sgfx_font_builtin.h — compact 5x7 ASCII font (32..126), public-domain style data.
// Column-major: 5 bytes per glyph; bit 0..6 = rows top..bottom.

#pragma once
#include <stdint.h>
#include <stdbool.h>

// Forward-declare SGFX types only if you want to use the optional draw helper.
#include "sgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns true and writes 5 column bytes for 'ch' (ASCII 32..126).
// Each column byte uses bits 0..6 for rows (top..bottom).
bool sgfx_font5x7_get(char ch, uint8_t out_col[5]);

// Metrics
static inline int sgfx_font5x7_width_px(void)  { return 5; }
static inline int sgfx_font5x7_height_px(void) { return 7; }
static inline int sgfx_font5x7_advance_px(void){ return 6; } // +1 spacing

// Optional tiny renderer (solid pixels, integer scaling). Uses only sgfx_fill_rect.
int sgfx_font5x7_draw(sgfx_device_t* d, int x, int y,
                      const char* s, sgfx_rgba8_t c, int sx, int sy);

#ifdef __cplusplus
}
#endif