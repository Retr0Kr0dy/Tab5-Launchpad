#include "studio_ui.h"
#include <stdio.h>
#include <string.h>
#include "studio_font.inc"
#define COLOR(r,g,b) {r,g,b,255}
const sgfx_rgba8_t SU_BG=COLOR(16,20,26), SU_PANEL=COLOR(26,32,41), SU_RAISED=COLOR(35,44,55), SU_EDGE=COLOR(55,67,80), SU_TEXT=COLOR(235,241,246), SU_DIM=COLOR(159,175,190), SU_ACCENT=COLOR(100,218,204), SU_GREEN=COLOR(125,224,153), SU_AMBER=COLOR(247,194,111), SU_RED=COLOR(255,132,137), SU_VIOLET=COLOR(181,166,242);
void su_fill(sgfx_device_t*d,su_rect r,sgfx_rgba8_t c) {
    sgfx_fill_rect(d,r.x,r.y,r.w,r.h,c);
}
int su_contains(su_rect r,int x,int y) {
    return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;
}
typedef struct  {
    const uint8_t*data;
    const uint32_t*offset;
    const uint8_t*width;
    int h;
}
font_t;
#define FONT(n) (font_t){font##n##_data,font##n##_offset,font##n##_width,n+6}
static font_t font(int size) {
    if(size>=48)return FONT(48);
    if(size>=32)return FONT(32);
    if(size>=24)return FONT(24);
    return FONT(18);
}
int su_text_width(const char*s,int size) {
    font_t f=font(size);
    int w=0;
    for(; *s; s++) {
        unsigned c=(unsigned char)*s;
        if(c<32||c>126)c='?';
        w+=f.width[c-32];
    }
    return w;
}
/* A vertical-run-merge optimization was tried here first (buffer
 * consecutive identical rows of a glyph and flush as one taller fill_rect
 * instead of many 1px-tall ones). Measured a real ~15-20% call-count
 * reduction, but a pixel-diff against the original baseline (`compare
 * -metric AE`, native preview tool) found ~900 differing pixels on real
 * glyphs ('2', 'M', 'm') -- a confirmed rendering bug, reverted rather
 * than ship it (see CLAUDE.md's Rendering section for the full account).
 *
 * This is the real fix that replaced it: compose each glyph into a small
 * local RGB565 buffer (every pixel, antialiased level included -- palette
 * index 0 always equals `bg` by construction, so "transparent" pixels are
 * just bg-colored pixels, not literally skipped, matching what the old
 * per-run `if(a)` skip achieved implicitly) and `sgfx_blit()` it in one
 * shot, instead of one `fill_rect()` call per decoded RLE run. Needs real
 * `set_window`/`write_pixels` ops on the DSI driver side (`display.c`) --
 * those didn't exist before this pass; see that file's own comment on the
 * rotation math `write_pixels()` has to do, which is NOT exercised by the
 * native preview tool (its host backend's set_window/write_pixels are
 * unrotated) and needed real on-device verification, not just a clean
 * preview pixel-diff. Buffer is sized to the largest glyph this font set
 * actually has (48pt: 49px wide, 54px tall) with headroom; `static`, not
 * on the stack -- this is 8KB and su_text() is only ever called from the
 * single UI task, never reentrantly. */
#define SU_GLYPH_BUF_W 64
#define SU_GLYPH_BUF_H 64
void su_text(sgfx_device_t*d,int x,int y,const char*s,int size,sgfx_rgba8_t fg,sgfx_rgba8_t bg) {
    font_t f=font(size);
    sgfx_rgba8_t palette[16];
    for(int a=0; a<16; a++)palette[a]=(sgfx_rgba8_t) {
        (fg.r*a+bg.r*(15-a))/15,(fg.g*a+bg.g*(15-a))/15,(fg.b*a+bg.b*(15-a))/15,255
    };
    uint16_t pal16[16];
    for(int a=0; a<16; a++)pal16[a]=(uint16_t)(((palette[a].r&0xF8)<<8)|((palette[a].g&0xFC)<<3)|(palette[a].b>>3));
    static uint16_t glyph_buf[SU_GLYPH_BUF_H][SU_GLYPH_BUF_W];
    int gh=f.h<SU_GLYPH_BUF_H?f.h:SU_GLYPH_BUF_H;
    for(; *s; s++) {
        unsigned c=(unsigned char)*s;
        if(c<32||c>126)c='?';
        int g=c-32,w=f.width[g];
        const uint8_t*p=f.data+f.offset[g];
        int gw=w<SU_GLYPH_BUF_W?w:SU_GLYPH_BUF_W;
        for(int yy=0; yy<f.h; yy++) {
            int xx=0;
            while(xx<w) {
                uint8_t run=*p++;
                int n=(run>>4)+1,a=run&15;
                if(yy<gh) {
                    uint16_t color=pal16[a];
                    int end=xx+n;
                    if(end>gw)end=gw;
                    for(int px=xx; px<end; px++)glyph_buf[yy][px]=color;
                }
                xx+=n;
            }
        }
        sgfx_blit(d,x,y,gw,gh,SGFX_FMT_RGB565,glyph_buf,SU_GLYPH_BUF_W*2);
        x+=w;
    }
}
void su_fit(sgfx_device_t*d,su_rect r,const char*s,int size,sgfx_rgba8_t fg,sgfx_rgba8_t bg) {
    char b[160];
    snprintf(b,sizeof b,"%.159s",s); /* explicit precision so -Wformat-truncation can prove this is safe; see ymodem.c's own comment on the same pattern -- `s` is caller-supplied, unbounded from the compiler's view */
    int n=(int)strlen(b);
    int clipped=0;
    while(n>0&&su_text_width(b,size)>r.w) {
        b[--n]=0;
        clipped=1;
    }
    if(clipped&&n>=3) {
        b[n-3]='.';
        b[n-2]='.';
        b[n-1]='.';
    }
    su_text(d,r.x,r.y,b,size,fg,bg);
}
void su_center(sgfx_device_t*d,su_rect r,const char*s,int size,sgfx_rgba8_t fg,sgfx_rgba8_t bg) {
    int tw=su_text_width(s,size);
    if(tw>r.w) {
        su_fit(d,(su_rect) {
            r.x+8,r.y+(r.h-size-6)/2,r.w-16,r.h
        },s,size,fg,bg);
        return;
    }
    su_text(d,r.x+(r.w-tw)/2,r.y+(r.h-size-6)/2,s,size,fg,bg);
}
void su_card(sgfx_device_t*d,su_rect r,sgfx_rgba8_t bg) {
    su_fill(d,r,SU_EDGE);
    su_fill(d,(su_rect) {
        r.x+1,r.y+1,r.w-2,r.h-2
    },bg);
}
void su_button(sgfx_device_t*d,su_rect r,const char*s,int active) {
    su_card(d,r,active?SU_ACCENT:SU_RAISED);
    su_center(d,r,s,24,active?SU_BG:SU_TEXT,active?SU_ACCENT:SU_RAISED);
}
void su_header(sgfx_device_t*d,int w,const char*title,const char*sub) {
    su_text(d,24,14,"TAB5 /",18,SU_ACCENT,SU_BG);
    su_text(d,104,9,title,32,SU_TEXT,SU_BG);
    su_fit(d,(su_rect) {
        24,57,w-48,28
    },sub,18,SU_DIM,SU_BG);
    su_fill(d,(su_rect) {
        24,96,w-48,1
    },SU_EDGE);
}
void su_connection(sgfx_device_t*d,int x,int y,int ready,int host) {
    const char*s=!ready?"MIDI unavailable":host?"USB connected":"Connect USB";
    sgfx_rgba8_t c=!ready?SU_RED:host?SU_GREEN:SU_AMBER;
    su_fill(d,(su_rect) {
        x,y+9,8,8
    },c);
    su_text(d,x+18,y,s,18,c,SU_BG);
}
su_rect su_back_rect(int w,int h) {
    (void)w;
    return(su_rect) {
        24,h-100,152,76
    };
}
su_rect su_home_tile(int w,int h,int i) {
    int wide=w>h;
    if(i==0)return(su_rect) {
        24,146,wide?(w-60)*2/3:w-48,wide?h-270:330
    };
    int x=wide?24+(w-60)*2/3+12:24;
    int y=wide?146+(i-1)*((h-282)/2+12):492+(i-1)*194;
    return(su_rect) {
        x,y,wide?w-x-24:w-48,wide?(h-282)/2:180
    };
}
void su_home_draw(sgfx_device_t*d,int w,int h,int ready,int host) {
    sgfx_clear(d,SU_BG);
    su_header(d,w,"Launchpad","A dedicated USB MIDI control surface");
    su_rect r=su_home_tile(w,h,0);
    su_card(d,r,SU_PANEL);
    su_fill(d,(su_rect) {
        r.x,r.y,4,r.h
    },SU_ACCENT);
    su_text(d,r.x+28,r.y+25,"PERFORMANCE",18,SU_ACCENT,SU_PANEL);
    su_text(d,r.x+28,r.y+66,"Play. Shape. Control.",32,SU_TEXT,SU_PANEL);
    su_text(d,r.x+28,r.y+118,"Notes / Drums / Faders / XY / Macros",18,SU_DIM,SU_PANEL);
    for(int i=0; i<8; i++)su_fill(d,(su_rect) {
        r.x+28+i*38,r.y+177,28,28
    },i==1||i==4?SU_ACCENT:SU_RAISED);
    su_button(d,(su_rect) {
        r.x+28,r.y+r.h-98,r.w-56,70
    },"Open controller  >",1);
    static const char*names[]= {
        "","Settings","Diagnostics"
    };
    static const char*subs[]= {
        "","Display and preferences","USB and device health"
    };
    for(int i=1; i<3; i++) {
        r=su_home_tile(w,h,i);
        su_card(d,r,SU_PANEL);
        su_text(d,r.x+24,r.y+20,i==1?"01 / SETUP":"02 / SYSTEM",18,SU_DIM,SU_PANEL);
        su_text(d,r.x+24,r.y+54,names[i],32,SU_TEXT,SU_PANEL);
        su_text(d,r.x+24,r.y+101,subs[i],18,SU_DIM,SU_PANEL);
    }
    su_connection(d,24,h-70,ready,host);
    su_text(d,w-250,h-70,"Manual MIDI mapping",18,SU_DIM,SU_BG);
}
su_rect su_setting_rect(int w,int h,int i) {
    (void)h;
    return(su_rect) {
        24,126+i*124,w-48,108
    };
}
void su_settings_draw(sgfx_device_t*d,int w,int h,int brightness,int rotate,int debug,int ready,int host,const char*build,const char*panel) {
    sgfx_clear(d,SU_BG);
    su_header(d,w,"Settings","Display preferences / controller stays in landscape while playing");
    for(int i=0; i<3; i++) {
        su_rect r=su_setting_rect(w,h,i);
        su_card(d,r,SU_PANEL);
        const char*name=i==0?"Display brightness":i==1?"Automatic rotation":"Performance overlay";
        const char*hint=i==0?"Drag to adjust":i==1?"Home, settings and diagnostics":"Frame rate and free memory";
        su_text(d,r.x+20,r.y+14,name,24,SU_TEXT,SU_PANEL);
        su_text(d,r.x+20,r.y+55,hint,18,SU_DIM,SU_PANEL);
        if(i==0) {
            int x=r.x+r.w/2,width=r.w/2-120;
            su_fill(d,(su_rect) {
                x,r.y+50,width,6
            },SU_EDGE);
            su_fill(d,(su_rect) {
                x,r.y+50,width*brightness/100,6
            },SU_ACCENT);
            su_fill(d,(su_rect) {
                x+width*brightness/100-4,r.y+34,8,38
            },SU_ACCENT);
            char b[16];
            snprintf(b,sizeof b,"%d%%",brightness);
            su_text(d,r.x+r.w-90,r.y+35,b,24,SU_TEXT,SU_PANEL);
        } else  {
            int on=i==1?rotate:debug;
            su_button(d,(su_rect) {
                r.x+r.w-144,r.y+18,124,72
            },on?"On":"Off",on);
        }
    }
    su_connection(d,24,515,ready,host);
    su_fit(d,(su_rect) {
        24,550,w-48,28
    },panel,18,SU_DIM,SU_BG);
    su_button(d,su_back_rect(w,h),"< Home",0);
    su_fit(d,(su_rect) {
        200,h-80,w-224,30
    },build,18,SU_DIM,SU_BG);
}
int su_info_page_size(int h) {
    int n=(h-280)/64;
    return n<1?1:n;
}
su_rect su_page_rect(int w,int h,int next) {
    return(su_rect) {
        w-(next?176:340),h-100,152,76
    };
}
void su_diagnostics_draw(sgfx_device_t*d,int w,int h,const su_info_row*rows,int count,int page,int ready,int host) {
    sgfx_clear(d,SU_BG);
    su_header(d,w,"Diagnostics","Device health / USB connection does not confirm a DAW mapping");
    int n=su_info_page_size(h);
    for(int i=0; i<n&&page*n+i<count; i++) {
        su_rect r= {
            24,120+i*64,w-48,62
        };
        su_fill(d,r,i%2?SU_BG:SU_PANEL);
        const su_info_row*row=&rows[page*n+i];
        int labelw=w>h?240:180;
        su_fit(d,(su_rect) {
            40,r.y+16,labelw-16,32
        },row->label,18,SU_DIM,i%2?SU_BG:SU_PANEL);
        su_fit(d,(su_rect) {
            40+labelw,r.y+12,w-labelw-80,36
        },row->value,24,SU_TEXT,i%2?SU_BG:SU_PANEL);
    }
    su_connection(d,24,h-148,ready,host);
    char s[40];
    snprintf(s,sizeof s,"Page %d / %d",page+1,(count+n-1)/n);
    su_text(d,w-190,h-148,s,18,SU_DIM,SU_BG);
    su_button(d,su_back_rect(w,h),"< Home",0);
    su_button(d,su_page_rect(w,h,0),"< Previous",0);
    su_button(d,su_page_rect(w,h,1),"Next >",0);
}
void su_splash_draw(sgfx_device_t*d,int w,int h,const char*version) {
    sgfx_clear(d,SU_BG);
    int x=w/2-94,y=h/2-135;
    for(int r=0; r<3; r++)for(int c=0; c<5; c++)su_fill(d,(su_rect) {
        x+c*40,y+r*32,28,20
    },r==1&&c>1?SU_ACCENT:SU_RAISED);
    su_center(d,(su_rect) {
        0,h/2-10,w,70
    },"Tab5 Launchpad",48,SU_TEXT,SU_BG);
    su_center(d,(su_rect) {
        0,h/2+72,w,38
    },"USB MIDI / Performance controller",24,SU_DIM,SU_BG);
    su_center(d,(su_rect) {
        0,h-80,w,34
    },version,18,SU_DIM,SU_BG);
}
