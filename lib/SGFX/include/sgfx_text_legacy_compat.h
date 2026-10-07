#pragma once
#include "sgfx_text.h"
/* Map old fixed 5x7 API to new engine using builtin font at small px */
static inline void fb_draw_5x7_compat(sgfx_fb_t* fb,int x,int y,const char* s, sgfx_rgba8_t c){
  sgfx_font_t* F = sgfx_font_open_builtin();
  sgfx_text_style_t st = sgfx_text_style_default(c, 10.f);
  sgfx_text_draw_line(fb,x,y,s,F,&st);
}