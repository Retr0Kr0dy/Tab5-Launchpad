/*
 * launchpad_preview.c — host preview for the Launchpad screen's real SGFX
 * rendering layer.
 *
 * Same idea as tools/ui_preview.c: links main/launchpad_grid.c and
 * main/launchpad_modes.c verbatim and drives them through a tiny RGB565
 * SGFX host driver, so these PPM frames are exactly what the firmware
 * would draw, not a reimplementation. Only landscape (1280x720) is
 * rendered -- main/launchpad_app.c force-pushes rotation 1 for this screen
 * (see its header comment), so that is the only orientation this screen
 * ever actually runs in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "sgfx.h"
#include "launchpad_grid.h"
#include "launchpad_modes.h"
#include "studio_ui.h"

#define FB_W 1280
#define FB_H 720

static int g_w, g_h;
static uint16_t fb[FB_W * FB_H];
static struct { int x, y, w, h, cursor; } win;

static uint16_t pack565(sgfx_rgba8_t c)
{
    return (uint16_t)(((c.r & 0xF8u) << 8) | ((c.g & 0xFCu) << 3) | (c.b >> 3));
}
static int h_begin(sgfx_bus_t* b){(void)b;return 0;}
static void h_end(sgfx_bus_t* b){(void)b;}
static int h_set_window(sgfx_device_t* d,int x,int y,int w,int h)
{ (void)d; win=(typeof(win)){x,y,w,h,0}; return 0; }
/* Perf investigation instrumentation -- count fill_rect calls and total
 * pixels actually written by a single lp_draw_frame() call, to find out
 * whether the real cost is call-count (text rendering issuing thousands
 * of tiny 1xN runs) or pixel-count (large area fills), since these have
 * very different fixes. See g_perf_report() below. */
static long g_fill_calls, g_fill_pixels;
static int h_fill(sgfx_device_t* d,int x,int y,int w,int h,sgfx_rgba8_t c)
{
    (void)d;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    if(w<=0||h<=0)return 0;
    g_fill_calls++;
    g_fill_pixels+=(long)w*h;
    uint16_t v=pack565(c);
    for(int yy=0;yy<h;yy++){
        uint16_t* row=&fb[(y+yy)*g_w+x];
        for(int xx=0;xx<w;xx++)row[xx]=v;
    }
    return 0;
}
static int h_write_pixels(sgfx_device_t* d,const void* px,size_t count,sgfx_pixfmt_t fmt)
{
    (void)d;
    if(fmt!=SGFX_FMT_RGB565)return SGFX_ERR_NOSUP;
    const uint16_t* p=(const uint16_t*)px;
    for(size_t i=0;i<count;i++){
        int off=win.cursor++;
        int yy=off/win.w, xx=off%win.w;
        if(xx<win.w && yy<win.h){
            int dx=win.x+xx,dy=win.y+yy;
            if((unsigned)dx<(unsigned)g_w&&(unsigned)dy<(unsigned)g_h)fb[dy*g_w+dx]=p[i];
        }
    }
    return 0;
}
static int h_present(sgfx_device_t* d){(void)d;return 0;}
static int h_init(sgfx_device_t* d){(void)d;return 0;}
static int h_write_cmd(sgfx_bus_t*b,uint8_t c){(void)b;(void)c;return 0;}
static int h_write_data(sgfx_bus_t*b,const void*p,size_t n){(void)b;(void)p;(void)n;return 0;}
static int h_repeat(sgfx_bus_t*b,const void*p,size_t a,size_t n){(void)b;(void)p;(void)a;(void)n;return 0;}
static int h_bus_pixels(sgfx_bus_t*b,const void*p,size_t n,sgfx_pixfmt_t f){(void)b;(void)p;(void)n;(void)f;return 0;}
static int h_read(sgfx_bus_t*b,void*p,size_t n){(void)b;(void)p;(void)n;return SGFX_ERR_NOSUP;}
static void h_delay(sgfx_bus_t*b,uint32_t n){(void)b;(void)n;}
static void h_gpio(sgfx_bus_t*b,int p,bool l){(void)b;(void)p;(void)l;}

static const sgfx_bus_ops_t bus_ops={h_begin,h_end,h_write_cmd,h_write_data,h_repeat,h_bus_pixels,h_read,h_delay,h_gpio};
static const sgfx_driver_ops_t drv_ops={
    .init=h_init,.set_window=h_set_window,.write_pixels=h_write_pixels,
    .fill_rect=h_fill,.present=h_present
};

static void rgb_from_565(uint16_t v,unsigned char* r,unsigned char* g,unsigned char* b)
{
    *r=(unsigned char)((((v>>11)&31)*255+15)/31);
    *g=(unsigned char)((((v>>5)&63)*255+31)/63);
    *b=(unsigned char)(((v&31)*255+15)/31);
}
static int save_ppm(const char* path)
{
    FILE* f=fopen(path,"wb"); if(!f)return -1;
    fprintf(f,"P6\n%d %d\n255\n",g_w,g_h);
    for(int i=0;i<g_w*g_h;i++){
        unsigned char rgb[3]; rgb_from_565(fb[i],&rgb[0],&rgb[1],&rgb[2]);
        fwrite(rgb,1,3,f);
    }
    fclose(f); return 0;
}

static void render_one(sgfx_device_t* dev, const char* outdir, const char* name, const lp_grid_model_t* m)
{
    char path[512];
    g_fill_calls = 0; g_fill_pixels = 0;
    lp_draw_frame(dev, g_w, g_h, m);
    fprintf(stderr, "[perf] %-24s fill_rect calls=%6ld  pixels=%8ld\n", name, g_fill_calls, g_fill_pixels);
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    save_ppm(path);
}

int main(int argc, char** argv)
{
    const char* outdir = (argc > 1) ? argv[1] : "launchpad-preview";
    g_w = FB_W; g_h = FB_H;

    sgfx_bus_t bus = { .ops = &bus_ops };
    sgfx_device_t dev;
    uint16_t scratch[1024];
    sgfx_caps_t caps = { FB_W, FB_H, SGFX_FMT_RGB565, 16, 0 };
    if (sgfx_init(&dev, &bus, &drv_ops, &caps, scratch, sizeof scratch) != 0) {
        return 2;
    }

    lp_grid_model_t m;

    /* 1: Note mode, idle. */
    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_NOTE;
    m.midi_active = 1; m.host_connected = 1;
    render_one(&dev, outdir, "01_note_idle", &m);

    /* 2: Note mode, a few pads held -- checks the pressed-color flash and
     * that per-cell state doesn't bleed into neighbors. */
    m.pad_down[0] = 1; m.pad_down[5] = 1; m.pad_down[31] = 1;
    render_one(&dev, outdir, "02_note_pressed", &m);

    /* 3: Drum mode, idle -- different idle tint, different grid shape. */
    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_DRUM;
    m.channel = 9;
    m.midi_active = 1; m.host_connected = 0; /* amber "no host" status */
    render_one(&dev, outdir, "03_drum_idle", &m);
    m.pad_down[12] = 1; m.pad_down[13] = 1; /* kick + snare */
    render_one(&dev, outdir, "04_drum_pressed", &m);

    /* 5: Mixer-CC mode, default variant (4 faders) -- a spread of values
     * (near-zero, mid, near-max, zero) to check the bottom-anchored bar
     * math, plus one column marked as currently being dragged (brighter
     * fill) to check that highlight against the "parked" color. */
    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_MIXER_CC;
    m.midi_active = 0; m.host_connected = 0; /* red "MIDI OFF" status */
    m.mixer_value[0] = 10; m.mixer_value[1] = 64; m.mixer_value[2] = 120; m.mixer_value[3] = 0;
    m.mixer_touch_active[1] = 1; /* column 1 is "being dragged" right now */
    render_one(&dev, outdir, "05_mixer_cc", &m);

    /* 6: XY-macro mode -- single surface, crosshair rendering, both at
     * center and near an edge (clamp check). */
    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_XY_MACRO;
    m.midi_active = 1; m.host_connected = 1;
    lp_rect_t area = lp_grid_area(g_w, g_h);
    m.xy_active = 1; m.xy_x = area.x + area.w / 2; m.xy_y = area.y + area.h / 2;
    render_one(&dev, outdir, "06_xy_center", &m);
    m.xy_x = area.x + 2; m.xy_y = area.y + area.h - 2; /* bottom-left corner */
    render_one(&dev, outdir, "07_xy_corner", &m);

    /* 8-10: DIM/CH footer buttons -- every NOTE/MIXER-CC variant, plus the
     * widest channel label ("CH 16"), checked for button-text centering
     * and no overlap with the status block to its right. */
    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_NOTE;
    m.midi_active = 1; m.host_connected = 1;
    m.channel = 15; /* "CH 16" -- widest channel label */
    for (int v = 0; v < lp_mode_variant_count(LP_MODE_NOTE); v++) {
        m.variant = v;
        char name[32];
        snprintf(name, sizeof name, "08_note_variant%d", v);
        render_one(&dev, outdir, name, &m);
    }
    m.mode = LP_MODE_MIXER_CC;
    memset(m.mixer_touch_active, 0, sizeof m.mixer_touch_active);
    for (int v = 0; v < lp_mode_variant_count(LP_MODE_MIXER_CC); v++) {
        m.variant = v;
        /* A spread of values across whatever column count this variant
         * has -- checks the fader-width math at every density (4/8/2). */
        int cols = lp_mode_mixer_columns(v);
        for (int c = 0; c < cols && c < LP_MIXER_MAX_COLS; c++) {
            m.mixer_value[c] = (uint8_t)(((c + 1) * 127) / cols);
        }
        char name[32];
        snprintf(name, sizeof name, "09_mixer_variant%d", v);
        render_one(&dev, outdir, name, &m);
    }

    /* 10: Knob mode -- every variant (8/4/16 knobs), a spread of values
     * (0/64/127/mid-high) across cells to check pointer-angle rendering
     * and cell layout at each density. */
    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_KNOB;
    m.midi_active = 1; m.host_connected = 1;
    for (int v = 0; v < lp_mode_variant_count(LP_MODE_KNOB); v++) {
        m.variant = v;
        lp_grid_shape_t shape = lp_mode_grid_shape(LP_MODE_KNOB, v);
        int n = shape.rows * shape.cols;
        static const uint8_t kSpread[4] = { 0, 42, 85, 127 };
        for (int i = 0; i < n && i < LP_MAX_PADS; i++) {
            m.knob_value[i] = kSpread[i % 4];
        }
        char name[32];
        snprintf(name, sizeof name, "10_knob_variant%d", v);
        render_one(&dev, outdir, name, &m);
    }

    /* 17: Looper mode -- every state (idle/recording/playing/paused) on
     * the SELECTED track, to check button highlight colors, the progress
     * bar fill math, and text layout at each state's different content
     * (event count/duration line only has something to say once
     * something's recorded). Plus one frame with all 4 tracks in
     * different states at once, to check the track-selector row's
     * per-track color-coding and selection strip don't collide/overlap. */
    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_LOOPER;
    m.midi_active = 1; m.host_connected = 1;
    m.loop_bpm = 120;
    m.loop_state[0] = 0; /* IDLE */
    render_one(&dev, outdir, "17_looper_idle", &m);
    m.loop_state[0] = 1; /* RECORDING */
    render_one(&dev, outdir, "17_looper_recording", &m);
    m.loop_state[0] = 2; /* PLAYING */
    m.loop_count[0] = 42;
    m.loop_duration_ms[0] = 4300;
    m.loop_pos_ms[0] = 1200; /* ~28% through the bar */
    render_one(&dev, outdir, "17_looper_playing", &m);
    m.loop_state[0] = 3; /* OVERDUB */
    m.loop_pos_ms[0] = 3800;
    render_one(&dev, outdir, "17_looper_overdub", &m);
    m.loop_state[0] = 4; /* PAUSED */
    render_one(&dev, outdir, "17_looper_paused", &m);

    memset(&m, 0, sizeof m);
    m.mode = LP_MODE_LOOPER;
    m.midi_active = 1; m.host_connected = 1;
    m.loop_bpm = 140;
    m.loop_beat_pulse = 200; /* mid-flash, so the metronome dot is visibly lit in the still frame */
    m.loop_selected = 2;
    m.loop_state[0] = 2; m.loop_count[0] = 18; m.loop_duration_ms[0] = 2000; m.loop_pos_ms[0] = 900;
    m.loop_state[1] = 1; /* RECORDING */
    m.loop_state[2] = 3; m.loop_count[2] = 7; m.loop_duration_ms[2] = 4000; m.loop_pos_ms[2] = 3500; /* OVERDUB, selected */
    m.loop_state[3] = 4; m.loop_count[3] = 30; m.loop_duration_ms[3] = 1600; /* PAUSED */
    render_one(&dev, outdir, "17_looper_multitrack", &m);

    /* No delta/dirty-region test frames anymore -- lp_draw_frame_delta()
     * was removed (see launchpad_grid.h's comment on lp_draw_frame()): now
     * that display.c double-buffers for real, every redraw is a full
     * lp_draw_frame() call, so there is no separate partial-update path
     * left to stress-test here. */

    /* The same stateless view functions used by every firmware screen. */
    char path[512];
    m.route_open = 1;
    render_one(&dev,outdir,"11_channel_selector",&m);
    for (int portrait=0;portrait<2;portrait++) {
        g_w=portrait?720:1280;g_h=portrait?1280:720;
        dev.caps.width=g_w;dev.caps.height=g_h;sgfx_reset_clip(&dev);
        const char *suffix=portrait?"portrait":"landscape";
        su_home_draw(&dev,g_w,g_h,1,1);
        snprintf(path,sizeof path,"%s/12_home_%s.ppm",outdir,suffix);save_ppm(path);
        su_settings_draw(&dev,g_w,g_h,65,1,0,1,1,"Firmware 0.2.0 / preview","Display / ST7121");
        snprintf(path,sizeof path,"%s/13_settings_%s.ppm",outdir,suffix);save_ppm(path);
        su_info_row rows[]={
            {"Processor","ESP32-P4 / preview data"},{"Internal free","320 KB"},
            {"PSRAM free","24000 KB"},{"Battery","78% / 7.80 V"},
            {"SD card","Present"},{"Display","ST7121"},{"MIDI transport","USB device active"},
            {"Host","USB enumerated / DAW unverified"},{"Touch","ST7121"},
            {"Motion","BMI270"}
        };
        su_diagnostics_draw(&dev,g_w,g_h,rows,10,0,1,1);
        snprintf(path,sizeof path,"%s/14_diagnostics_%s.ppm",outdir,suffix);save_ppm(path);
        su_diagnostics_draw(&dev,g_w,g_h,rows,10,portrait?0:1,1,0);
        snprintf(path,sizeof path,"%s/15_diagnostics_page2_%s.ppm",outdir,suffix);save_ppm(path);
        su_splash_draw(&dev,g_w,g_h,"Firmware 0.2.0");
        snprintf(path,sizeof path,"%s/16_boot_%s.ppm",outdir,suffix);save_ppm(path);
    }
    printf("Launchpad preview frames written to %s\n", outdir);
    return 0;
}
