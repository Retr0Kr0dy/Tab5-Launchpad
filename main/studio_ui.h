#pragma once
#include "sgfx.h"
/* Portable, allocation-free presentation shared by firmware and native previews. */
typedef struct { int x,y,w,h; } su_rect;
extern const sgfx_rgba8_t SU_BG, SU_PANEL, SU_RAISED, SU_EDGE, SU_TEXT, SU_DIM,
    SU_ACCENT, SU_GREEN, SU_AMBER, SU_RED, SU_VIOLET;
void su_fill(sgfx_device_t*, su_rect, sgfx_rgba8_t);
int su_contains(su_rect,int,int);
int su_text_width(const char*,int);
void su_text(sgfx_device_t*,int,int,const char*,int,sgfx_rgba8_t,sgfx_rgba8_t);
void su_fit(sgfx_device_t*,su_rect,const char*,int,sgfx_rgba8_t,sgfx_rgba8_t);
void su_center(sgfx_device_t*,su_rect,const char*,int,sgfx_rgba8_t,sgfx_rgba8_t);
void su_card(sgfx_device_t*,su_rect,sgfx_rgba8_t);
void su_button(sgfx_device_t*,su_rect,const char*,int);
void su_header(sgfx_device_t*,int,const char*,const char*);
void su_connection(sgfx_device_t*,int,int,int,int);
su_rect su_back_rect(int,int);
su_rect su_home_tile(int,int,int);
void su_home_draw(sgfx_device_t*,int,int,int,int);
su_rect su_setting_rect(int,int,int);
void su_settings_draw(sgfx_device_t*,int,int,int,int,int,int,int,const char*,const char*);
#define SU_INFO_MAX 12
typedef struct { char label[32], value[96]; } su_info_row;
su_rect su_page_rect(int,int,int);
int su_info_page_size(int);
void su_diagnostics_draw(sgfx_device_t*,int,int,const su_info_row*,int,int,int,int);
void su_splash_draw(sgfx_device_t*,int,int,const char*);
