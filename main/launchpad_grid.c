/* Studio UI: geometry is shared by rendering and hit-testing. No ESP-IDF. */
#include "launchpad_grid.h"
#include "studio_ui.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static su_rect sr(lp_rect_t r) {
    return(su_rect) {
        r.x,r.y,r.w,r.h
    };
}
lp_rect_t lp_grid_area(int w,int h) {
    return(lp_rect_t) {
        24,184,w-48,h-300
    };
}
lp_rect_t lp_mode_tab_rect(int w,int h,int i) {
    (void)h;
    int cw=(w-48-(LP_MODE_COUNT-1)*8)/LP_MODE_COUNT;
    return(lp_rect_t) {
        24+i*(cw+8),60,cw,66
    };
}
static su_rect home_rect(int w,int h) {
    return su_back_rect(w,h);
}
static su_rect route_rect(int w,int h) {
    (void)w;
    return(su_rect) {
        188,h-100,202,76
    };
}
static su_rect dim_rect(int w,int h) {
    (void)w;
    return(su_rect) {
        402,h-100,214,76
    };
}
static su_rect panic_rect(int w,int h) {
    return(su_rect) {
        w-184,h-100,160,76
    };
}
static su_rect cell(lp_rect_t a,int rows,int cols,int row,int col) {
    int cw=(a.w-(cols-1)*10)/cols,ch=(a.h-(rows-1)*10)/rows;
    return(su_rect) {
        a.x+col*(cw+10),a.y+row*(ch+10),cw,ch
    };
}
static su_rect channel_rect(int w,int h,int i) {
    int cw=(w-96-36)/4,ch=(h-260-36)/4;
    return(su_rect) {
        48+(i%4)*(cw+12),144+(i/4)*(ch+12),cw,ch
    };
}
static void line(sgfx_device_t*d,int x,int y,int xx,int yy,sgfx_rgba8_t c) {
    int dx=abs(xx-x),sx=x<xx?1:-1,dy=-abs(yy-y),sy=y<yy?1:-1,err=dx+dy;
    for(; ; ) {
        sgfx_fill_rect(d,x,y,2,2,c);
        if(x==xx&&y==yy)break;
        int e=2*err;
        if(e>=dy) {
            err+=dy;
            x+=sx;
        }
        if(e<=dx) {
            err+=dx;
            y+=sy;
        }
    }
}
static const char* drum_name(int i) {
    static const char*const names[]= {
        "Crash 1","Ride 1","Crash 2","Ride 2","Low tom","Low-mid tom","High-mid tom","High tom","Clap","Closed hat","Pedal hat","Open hat","Acoustic kick","Kick","Acoustic snare","Electric snare"
    };
    return names[i%16];
}
static void pads(sgfx_device_t*d,lp_rect_t a,const lp_grid_model_t*m) {
    /* Cards cover each cell but not the gaps between them -- fill the
     * whole area first so nothing relies on the (removed, see
     * lp_draw_frame()) full-canvas clear to paint those gaps. */
    su_fill(d,sr(a),SU_BG);
    lp_grid_shape_t s=lp_mode_grid_shape(m->mode,m->variant);
    static const char*const notes[]= {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    for(int row=0; row<s.rows; row++)for(int col=0; col<s.cols; col++) {
        int i=row*s.cols+col;
        su_rect r=cell(a,s.rows,s.cols,row,col);
        lp_msg_result_t msg=lp_mode_pad_event(m->mode,m->variant,i,1);
        int note=msg.msg.number;
        int down=m->pad_down[i];
        sgfx_rgba8_t accent=m->mode==LP_MODE_DRUM?(row==3?SU_AMBER:row==2?SU_ACCENT:SU_VIOLET):(note%12==0?SU_ACCENT:SU_DIM);
        sgfx_rgba8_t bg=down?accent:SU_PANEL,fg=down?SU_BG:SU_TEXT;
        su_card(d,r,bg);
        su_fill(d,(su_rect) {
            r.x+1,r.y+1,r.w-2,3
        },accent);
        char text[40];
        if(m->mode==LP_MODE_DRUM)snprintf(text,sizeof text,"%.39s",drum_name(i)); /* explicit precision so -Wformat-truncation can prove this is safe; see ymodem.c's own comment on the same pattern */
        else snprintf(text,sizeof text,"%s%d",notes[note%12],note/12-1);
        su_fit(d,(su_rect) {
            r.x+16,r.y+14,r.w-32,40
        },text,m->mode==LP_MODE_DRUM?24:32,fg,bg);
        snprintf(text,sizeof text,"%03d%s",note,down?" / ON":"");
        su_text(d,r.x+16,r.y+r.h-34,text,18,down?SU_BG:SU_DIM,bg);
    }
}
static void faders(sgfx_device_t*d,lp_rect_t a,const lp_grid_model_t*m) {
    /* Same reasoning as pads(): per-column cards don't cover the gaps
     * between columns. */
    su_fill(d,sr(a),SU_BG);
    int cols=lp_mode_mixer_columns(m->variant);
    for(int i=0; i<cols; i++) {
        su_rect r=cell(a,1,cols,0,i);
        su_card(d,r,SU_PANEL);
        char s[32];
        snprintf(s,sizeof s,"CC %d",20+i);
        su_center(d,(su_rect) {
            r.x,r.y+10,r.w,32
        },s,18,SU_DIM,SU_PANEL);
        snprintf(s,sizeof s,"%03u",m->mixer_value[i]);
        su_center(d,(su_rect) {
            r.x,r.y+42,r.w,44
        },s,32,SU_TEXT,SU_PANEL);
        int top=r.y+108,bottom=r.y+r.h-35,cy=bottom-m->mixer_value[i]*(bottom-top)/127,cx=r.x+r.w/2;
        su_fill(d,(su_rect) {
            cx-3,top,6,bottom-top
        },SU_EDGE);
        su_fill(d,(su_rect) {
            cx-3,cy,6,bottom-cy
        },SU_ACCENT);
        for(int k=0; k<5; k++) {
            int y=top+k*(bottom-top)/4;
            su_fill(d,(su_rect) {
                cx-24,y,10,1
            },SU_EDGE);
            su_fill(d,(su_rect) {
                cx+14,y,10,1
            },SU_EDGE);
        }
        sgfx_rgba8_t c=m->mixer_touch_active[i]?SU_ACCENT:SU_TEXT;
        su_fill(d,(su_rect) {
            cx-30,cy-9,60,18
        },c);
        su_fill(d,(su_rect) {
            cx-18,cy,36,1
        },SU_BG);
    }
}
static void knobs(sgfx_device_t*d,lp_rect_t a,const lp_grid_model_t*m) {
    /* Same reasoning as pads()/faders(): per-cell cards don't cover the
     * gaps between knob cells. */
    su_fill(d,sr(a),SU_BG);
    lp_grid_shape_t s=lp_mode_grid_shape(m->mode,m->variant);
    for(int row=0; row<s.rows; row++)for(int col=0; col<s.cols; col++) {
        int i=row*s.cols+col;
        su_rect r=cell(a,s.rows,s.cols,row,col);
        su_card(d,r,SU_PANEL);
        char text[24];
        snprintf(text,sizeof text,"CC %d",102+i);
        su_text(d,r.x+14,r.y+8,text,18,SU_DIM,SU_PANEL);
        int dense=s.rows==4,cx=dense?r.x+r.w-50:r.x+r.w/2,cy=dense?r.y+r.h/2:r.y+r.h/2+4,rad=dense?29:(r.h-70)/2;
        if(rad>65)rad=65;
        for(int k=0; k<=40; k++) {
            float angle=(-135.0f+270.0f*k/40)*3.14159265f/180;
            int x=cx+(int)(rad*sinf(angle)),y=cy-(int)(rad*cosf(angle));
            su_fill(d,(su_rect) {
                x-2,y-2,4,4
            },k*127<=m->knob_value[i]*40?SU_VIOLET:SU_EDGE);
        }
        float angle=(-135.0f+270.0f*m->knob_value[i]/127)*3.14159265f/180;
        line(d,cx,cy,cx+(int)((rad-9)*sinf(angle)),cy-(int)((rad-9)*cosf(angle)),SU_TEXT);
        snprintf(text,sizeof text,"%03u",m->knob_value[i]);
        if(dense)su_text(d,r.x+14,r.y+36,text,32,SU_TEXT,SU_PANEL);
        else su_center(d,(su_rect) {
            r.x,r.y+r.h-40,r.w,36
        },text,24,SU_TEXT,SU_PANEL);
    }
}
/* Shared by looper() (given `a` directly, like every other per-mode
 * renderer) and the lp_loop_*_hit() functions (given w/h, which compute
 * `a` themselves via lp_grid_area()) -- one place computing this geometry
 * so draw and hit-test can never drift apart, same discipline every other
 * control on this screen already uses. */
#define LP_LOOP_TRACK_ROW_H 70
#define LP_LOOP_TRACK_GAP 16
static su_rect loop_track_rect(lp_rect_t a,int index) {
    int bw=(a.w-(LP_LOOP_TRACK_COUNT-1)*LP_LOOP_TRACK_GAP)/LP_LOOP_TRACK_COUNT;
    return(su_rect) {
        a.x+index*(bw+LP_LOOP_TRACK_GAP),a.y,bw,LP_LOOP_TRACK_ROW_H
    };
}
/* Everything below the track-selector row shifts down by that row's
 * height + gap -- computed once here so the transport row, status text,
 * and BPM row (and their hit-tests) all agree on where that is. */
static lp_rect_t loop_transport_area(lp_rect_t a) {
    int off=LP_LOOP_TRACK_ROW_H+LP_LOOP_TRACK_GAP;
    return(lp_rect_t) {
        a.x,a.y+off,a.w,a.h-off
    };
}
static su_rect loop_btn_rect(lp_rect_t a,int index) {
    lp_rect_t t=loop_transport_area(a);
    int bw=(t.w-2*20)/3;
    return(su_rect) {
        t.x+index*(bw+20),t.y,bw,100
    };
}
#define LP_LOOP_BPM_BTN_W 48
/* Vertical offset from the transport row's own top (t.y) down to the BPM
 * row: rec button (100) + gap (28) + state text (48) + event/duration
 * line (40) + progress bar (16) + gap (20) -- must track looper()'s own
 * running `y` increments exactly, since this is a fixed offset (the
 * layout above it is never data-dependent in height, only in content),
 * not something recomputed per frame. */
#define LP_LOOP_BPM_ROW_OFFSET (100+28+48+40+16+20)
static su_rect loop_bpm_down_rect(lp_rect_t a) {
    lp_rect_t t=loop_transport_area(a);
    return(su_rect) {
        t.x,t.y+LP_LOOP_BPM_ROW_OFFSET,LP_LOOP_BPM_BTN_W,44
    };
}
static su_rect loop_bpm_up_rect(lp_rect_t a) {
    lp_rect_t t=loop_transport_area(a);
    /* Clear of the "-" button, the "NNN BPM" text between them (BPM is
     * clamped to 20-300, so at most 3 digits), and still leaves room for
     * the pulse dot after it. */
    return(su_rect) {
        t.x+280,t.y+LP_LOOP_BPM_ROW_OFFSET,LP_LOOP_BPM_BTN_W,44
    };
}
static const char* loop_state_name(int s) {
    switch(s) {
    case 1: return "RECORDING";
    case 2: return "PLAYING";
    case 3: return "OVERDUBBING";
    case 4: return "PAUSED";
    default: return "IDLE";
    }
}
static sgfx_rgba8_t loop_track_color(int state) {
    switch(state) {
    case 1: return SU_RED;     /* RECORDING */
    case 2: return SU_GREEN;   /* PLAYING */
    case 3: return SU_AMBER;   /* OVERDUB */
    case 4: return SU_VIOLET;  /* PAUSED */
    default: return SU_PANEL;  /* IDLE */
    }
}
static const char* loop_track_abbr(int state) {
    switch(state) {
    case 1: return "REC";
    case 2: return "PLAY";
    case 3: return "DUB";
    case 4: return "PAUSE";
    default: return "EMPTY";
    }
}
static sgfx_rgba8_t loop_blend(sgfx_rgba8_t a,sgfx_rgba8_t b,int t) {
    return(sgfx_rgba8_t) {
        (uint8_t)((a.r*t+b.r*(255-t))/255),(uint8_t)((a.g*t+b.g*(255-t))/255),
        (uint8_t)((a.b*t+b.b*(255-t))/255),255
    };
}
static void looper(sgfx_device_t*d,lp_rect_t a,const lp_grid_model_t*m) {
    su_fill(d,sr(a),SU_BG);
    int sel=m->loop_selected;

    /* Track selector row -- one button per track, color-coded by that
     * track's own state (independent tracks can be in different states
     * at once, e.g. track 0 PLAYING underneath while track 1 is still
     * being RECORDED). A bright strip along the bottom marks whichever
     * one Record/Play/Clear below currently target. */
    for(int i=0; i<LP_LOOP_TRACK_COUNT; i++) {
        su_rect r=loop_track_rect(a,i);
        sgfx_rgba8_t bg=loop_track_color(m->loop_state[i]);
        int lit=m->loop_state[i]!=0;
        su_card(d,r,bg);
        char label[16];
        snprintf(label,sizeof label,"T%d",i+1);
        su_text(d,r.x+12,r.y+10,label,24,lit?SU_BG:SU_TEXT,bg);
        su_text(d,r.x+12,r.y+r.h-28,loop_track_abbr(m->loop_state[i]),18,lit?SU_BG:SU_DIM,bg);
        if(i==sel)su_fill(d,(su_rect) {
            r.x+1,r.y+r.h-4,r.w-2,3
        },SU_ACCENT);
    }

    int state=m->loop_state[sel];
    int recording=state==1||state==3; /* RECORDING or OVERDUB */
    int playing=state==2||state==3;   /* PLAYING or OVERDUB */

    su_rect rec=loop_btn_rect(a,0);
    su_card(d,rec,recording?SU_RED:SU_PANEL);
    const char*rec_label=state==0?"Record":state==3?"Overdub ON":recording?"Recording":"Overdub";
    su_center(d,rec,rec_label,24,recording?SU_BG:SU_TEXT,recording?SU_RED:SU_PANEL);

    su_rect play=loop_btn_rect(a,1);
    su_card(d,play,playing?SU_ACCENT:SU_PANEL);
    su_center(d,play,playing?"Stop":"Play",24,playing?SU_BG:SU_TEXT,playing?SU_ACCENT:SU_PANEL);

    su_rect clr=loop_btn_rect(a,2);
    su_card(d,clr,SU_PANEL);
    su_center(d,clr,"Clear",24,SU_TEXT,SU_PANEL);

    int y=rec.y+rec.h+28;
    char line[64];
    snprintf(line,sizeof line,"Track %d: %.44s",sel+1,loop_state_name(state)); /* explicit precision so -Wformat-truncation can prove this is safe; see ymodem.c's own comment on the same pattern */
    su_text(d,a.x,y,line,32,SU_TEXT,SU_BG);
    y+=48;
    if(m->loop_count[sel]>0) {
        snprintf(line,sizeof line,"%d events / %u.%01us loop",m->loop_count[sel],
                 (unsigned)(m->loop_duration_ms[sel]/1000),(unsigned)((m->loop_duration_ms[sel]/100)%10));
    } else {
        snprintf(line,sizeof line,"Nothing recorded yet");
    }
    su_text(d,a.x,y,line,18,SU_DIM,SU_BG);
    y+=40;
    /* Loop-position progress bar -- only meaningful while this track is
     * actually looping. */
    su_fill(d,(su_rect) {
        a.x,y,a.w,16
    },SU_EDGE);
    if(playing&&m->loop_duration_ms[sel]>0) {
        int fill=(int)(((uint64_t)m->loop_pos_ms[sel]*(uint32_t)a.w)/m->loop_duration_ms[sel]);
        su_fill(d,(su_rect) {
            a.x,y,fill,16
        },SU_ACCENT);
    }

    /* BPM row: visual metronome only -- a tempo-feel aid, never used to
     * quantize or align recording/playback (see loop_bpm's comment in
     * launchpad_controller.h). The pulse dot flashes on every beat
     * regardless of what any track is doing. */
    su_rect down=loop_bpm_down_rect(a);
    su_card(d,down,SU_PANEL);
    su_center(d,down,"-",24,SU_TEXT,SU_PANEL);
    su_rect up=loop_bpm_up_rect(a);
    su_card(d,up,SU_PANEL);
    su_center(d,up,"+",24,SU_TEXT,SU_PANEL);
    snprintf(line,sizeof line,"%d BPM",m->loop_bpm);
    su_text(d,down.x+down.w+16,down.y+10,line,24,SU_TEXT,SU_BG);
    sgfx_rgba8_t dot=loop_blend(SU_ACCENT,SU_PANEL,m->loop_beat_pulse);
    su_fill(d,(su_rect) {
        up.x+up.w+20,up.y+8,28,28
    },dot);

    su_text(d,a.x,down.y+60,"Also captures MIDI received from your DAW while recording.",16,SU_DIM,SU_BG);
}
static void xy(sgfx_device_t*d,lp_rect_t a,const lp_grid_model_t*m) {
    su_card(d,sr(a),SU_PANEL);
    for(int i=1; i<8; i++)su_fill(d,(su_rect) {
        a.x+a.w*i/8,a.y+1,1,a.h-2
    },SU_RAISED);
    for(int i=1; i<4; i++)su_fill(d,(su_rect) {
        a.x+1,a.y+a.h*i/4,a.w-2,1
    },SU_RAISED);
    int x=m->xy_x,y=m->xy_y;
    if(x<a.x||x>=a.x+a.w||y<a.y||y>=a.y+a.h) {
        x=a.x+a.w/2;
        y=a.y+a.h/2;
    }
    if(m->xy_active||m->xy_valid) {
        /* Soft "diffusion" glow at the touch point instead of a hard
         * crosshair: concentric filled squares, largest+dimmest outermost
         * to smallest+brightest at the center -- a stepped radial falloff
         * (this graphics stack has no circle/alpha primitive). Each ring
         * is independently edge-clamped -- shrinking the far edge by
         * however much the near edge clamped inward, not just moving the
         * origin -- so no ring can bleed past the surface into the margin
         * next to it; an unclamped ring left a residue there in an earlier
         * version of this screen that nothing else ever repainted. */
        static const struct { int half; sgfx_rgba8_t color; } glow[] = {
            { 28, {  40,  70,  68, 255 } },
            { 20, {  70, 130, 122, 255 } },
            { 13, { 110, 190, 178, 255 } },
            {  6, { 100, 218, 204, 255 } },  /* hot core, matches SU_ACCENT */
        };
        for (int i = 0; i < (int)(sizeof glow / sizeof glow[0]); i++) {
            int half = glow[i].half;
            int rx = x - half, rw = 2 * half;
            if (rx < a.x) { rw -= (a.x - rx); rx = a.x; }
            if (rx + rw > a.x + a.w) rw = a.x + a.w - rx;
            int ry = y - half, rh = 2 * half;
            if (ry < a.y) { rh -= (a.y - ry); ry = a.y; }
            if (ry + rh > a.y + a.h) rh = a.y + a.h - ry;
            if (rw > 0 && rh > 0) su_fill(d, (su_rect){ rx, ry, rw, rh }, glow[i].color);
        }
    } else su_center(d,(su_rect) {
        a.x,a.y+a.h/2-25,a.w,50
    },"Touch to control / release to hold",24,SU_DIM,SU_PANEL);
    su_fill(d,(su_rect) {
        a.x+14,a.y+14,300,34
    },SU_PANEL);
    char axis[48];
    if(m->xy_valid || m->xy_active) snprintf(axis,sizeof axis,"Y / CC 74 / %03d",127-(y-a.y)*127/(a.h-1));
    else snprintf(axis,sizeof axis,"Y / CC 74 / --");
    su_text(d,a.x+20,a.y+16,axis,18,SU_TEXT,SU_PANEL);
    su_fill(d,(su_rect) {
        a.x+a.w-294,a.y+a.h-44,278,32
    },SU_PANEL);
    if(m->xy_valid || m->xy_active) snprintf(axis,sizeof axis,"X / CC 1 / %03d",(x-a.x)*127/(a.w-1));
    else snprintf(axis,sizeof axis,"X / CC 1 / --");
    su_text(d,a.x+a.w-286,a.y+a.h-42,axis,18,SU_TEXT,SU_PANEL);
}
/* Measured on real hardware: a plain sgfx_clear() over the whole canvas
 * every frame capped XY-drag redraws at ~14 FPS (should be ~57Hz, the
 * panel's real refresh rate -- see debug_overlay.h). The mode-tab row,
 * grid area and footer buttons already opaquely repaint ~90% of the
 * canvas themselves right after this runs, so clearing the whole thing
 * first is mostly wasted work. This still repaints every pixel every
 * frame (required under display.c's real double-buffering -- see
 * launchpad_grid.h's lp_draw_frame() comment for why a partial/skipped
 * region isn't safe there), just as several small fills sized to where
 * nothing else is about to draw, instead of one big one. */
static void draw_outer_margins(sgfx_device_t*d,int w,int h) {
    su_fill(d,(su_rect){0,0,w,60},SU_BG);            /* header/connection text band, above the mode row */
    su_fill(d,(su_rect){0,0,24,h},SU_BG);             /* left margin, full height */
    su_fill(d,(su_rect){w-24,0,24,h},SU_BG);          /* right margin, full height */
    su_fill(d,(su_rect){24,60,w-48,66},SU_BG);        /* mode-tab row's own band (covers inter-tab gaps) */
    su_fill(d,(su_rect){24,126,w-48,58},SU_BG);       /* hint-text band between the mode row and the grid area */
    su_fill(d,(su_rect){24,h-116,w-48,116},SU_BG);    /* footer band (covers inter-button gaps + the "LOCAL VALUES" caption) */
}

void lp_draw_frame(sgfx_device_t*d,int w,int h,const lp_grid_model_t*m) {
    if(m->route_open) {
        /* Infrequently entered, not continuously redrawn like a drag --
         * the plain clear here isn't worth the same treatment. */
        sgfx_clear(d,SU_BG);
        su_header(d,w,"MIDI channel","Select the output channel for this mode. Held notes have been released.");
        for(int i=0; i<16; i++) {
            char b[24];
            snprintf(b,sizeof b,"Channel %02d",i+1);
            su_button(d,channel_rect(w,h,i),b,m->channel==i);
        }
        su_button(d,su_back_rect(w,h),"< Cancel",0);
        return;
    }
    draw_outer_margins(d,w,h);
    su_text(d,24,12,"TAB5 / CONTROLLER",18,SU_ACCENT,SU_BG);
    su_connection(d,w-218,12,m->midi_active,m->host_connected);
    static const char*const names[]= {
        "Notes","Drums","Faders","XY","Macros","Loop"
    };
    for(int i=0; i<LP_MODE_COUNT; i++) {
        su_rect r=sr(lp_mode_tab_rect(w,h,i));
        su_card(d,r,SU_PANEL);
        su_center(d,r,names[i],24,i==(int)m->mode?SU_ACCENT:SU_DIM,SU_PANEL);
        if(i==(int)m->mode)su_fill(d,(su_rect) {
            r.x+1,r.y+r.h-4,r.w-2,3
        },SU_ACCENT);
    }
    const char*hint=m->mode==LP_MODE_NOTE?"Chromatic / fixed velocity 100":m->mode==LP_MODE_DRUM?"GM percussion / fixed velocity 110":m->mode==LP_MODE_MIXER_CC?"CC 20-27 / relative drag / local values":m->mode==LP_MODE_KNOB?"CC 102-117 / drag up or down / local values":m->mode==LP_MODE_LOOPER?"Records/replays whatever any mode sends":"CC 1 + 74 / absolute touch / hold on release";
    su_text(d,24,142,hint,18,SU_DIM,SU_BG);
    if(m->tx_failed)su_text(d,w-332,142,"Send failed / check USB",18,SU_RED,SU_BG);
    else su_text(d,w-300,142,"Generic MIDI / no DAW sync",18,SU_DIM,SU_BG);
    lp_rect_t a=lp_grid_area(w,h);
    if(m->mode==LP_MODE_XY_MACRO)xy(d,a,m);
    else if(m->mode==LP_MODE_MIXER_CC)faders(d,a,m);
    else if(m->mode==LP_MODE_KNOB)knobs(d,a,m);
    else if(m->mode==LP_MODE_LOOPER)looper(d,a,m);
    else pads(d,a,m);
    su_button(d,home_rect(w,h),"< Home",0);
    char b[32];
    snprintf(b,sizeof b,"Channel %02d",m->channel+1);
    su_button(d,route_rect(w,h),b,0);
    int count=lp_mode_variant_count(m->mode);
    lp_grid_shape_t s=lp_mode_grid_shape(m->mode,m->variant);
    if(m->mode==LP_MODE_XY_MACRO)snprintf(b,sizeof b,"XY surface");
    else if(m->mode==LP_MODE_MIXER_CC)snprintf(b,sizeof b,"%d faders  >",s.cols);
    else if(m->mode==LP_MODE_KNOB)snprintf(b,sizeof b,"%d macros  >",s.cols*s.rows);
    else if(m->mode==LP_MODE_LOOPER)snprintf(b,sizeof b,"Loop");
    else snprintf(b,sizeof b,"%d x %d%s",s.cols,s.rows,count>1?"  >":" / fixed");
    su_button(d,dim_rect(w,h),b,0);
    su_rect p=panic_rect(w,h);
    su_card(d,p,SU_PANEL);
    su_center(d,p,m->panic_sent?"Sent":"Panic",24,SU_RED,SU_PANEL);
    su_text(d,644,h-88,"LOCAL VALUES",18,SU_DIM,SU_BG);
    su_text(d,644,h-58,"Map controls in your DAW",18,SU_DIM,SU_BG);
}
int lp_grid_hit_pad(int w,int h,lp_mode_t mode,int variant,int x,int y) {
    if(mode==LP_MODE_MIXER_CC||mode==LP_MODE_XY_MACRO||mode==LP_MODE_LOOPER)return-1;
    lp_rect_t a=lp_grid_area(w,h);
    lp_grid_shape_t s=lp_mode_grid_shape(mode,variant);
    for(int r=0; r<s.rows; r++)for(int c=0; c<s.cols; c++)if(su_contains(cell(a,s.rows,s.cols,r,c),x,y))return r*s.cols+c;
    return-1;
}
int lp_mode_tab_hit(int w,int h,int x,int y) {
    for(int i=0; i<LP_MODE_COUNT; i++)if(su_contains(sr(lp_mode_tab_rect(w,h,i)),x,y))return i;
    return-1;
}
int lp_loop_rec_hit(int w,int h,int x,int y) {
    return su_contains(loop_btn_rect(lp_grid_area(w,h),0),x,y);
}
int lp_loop_playstop_hit(int w,int h,int x,int y) {
    return su_contains(loop_btn_rect(lp_grid_area(w,h),1),x,y);
}
int lp_loop_clear_hit(int w,int h,int x,int y) {
    return su_contains(loop_btn_rect(lp_grid_area(w,h),2),x,y);
}
int lp_loop_track_hit(int w,int h,int x,int y) {
    lp_rect_t a=lp_grid_area(w,h);
    for(int i=0; i<LP_LOOP_TRACK_COUNT; i++)if(su_contains(loop_track_rect(a,i),x,y))return i;
    return-1;
}
int lp_loop_bpm_down_hit(int w,int h,int x,int y) {
    return su_contains(loop_bpm_down_rect(lp_grid_area(w,h)),x,y);
}
int lp_loop_bpm_up_hit(int w,int h,int x,int y) {
    return su_contains(loop_bpm_up_rect(lp_grid_area(w,h)),x,y);
}
int lp_home_hit(int w,int h,int x,int y) {
    return su_contains(home_rect(w,h),x,y);
}
int lp_route_hit(int w,int h,int x,int y) {
    return su_contains(route_rect(w,h),x,y);
}
int lp_panic_hit(int w,int h,int x,int y) {
    return su_contains(panic_rect(w,h),x,y);
}
int lp_channel_cell_hit(int w,int h,int x,int y) {
    for(int i=0; i<16; i++)if(su_contains(channel_rect(w,h,i),x,y))return i;
    return-1;
}
int lp_dim_hit(int w,int h,int x,int y) {
    return su_contains(dim_rect(w,h),x,y);
}
int lp_mixer_col_hit(int w,int h,int variant,int x,int y) {
    int n=lp_mode_mixer_columns(variant);
    for(int i=0; i<n; i++)if(su_contains(cell(lp_grid_area(w,h),1,n,0,i),x,y))return i;
    return-1;
}
