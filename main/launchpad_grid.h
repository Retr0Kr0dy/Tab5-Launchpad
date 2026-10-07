#pragma once
#include "sgfx.h"
#include "launchpad_modes.h"
#ifdef __cplusplus
extern "C" {
#endif
#define LP_MAX_PADS 64
#define LP_MIXER_MAX_COLS 8
typedef struct {
    lp_mode_t mode;
    int variant;
    uint8_t channel; /* zero-based output channel */
    int pad_down[LP_MAX_PADS];
    int xy_active;
    int xy_x, xy_y; /* logical pixels; xy_valid retains position on release */
    uint8_t mixer_value[LP_MIXER_MAX_COLS];
    int mixer_touch_active[LP_MIXER_MAX_COLS];
    uint8_t knob_value[LP_MAX_PADS];
    int route_open, xy_valid, tx_failed, panic_sent;
    int midi_active, host_connected; /* USB enumeration, not DAW acknowledgment */
    /* Looper, mirrored from lp_controller for rendering -- see
     * launchpad_controller.h's lp_loop_state_t for the authoritative enum
     * (kept out of this platform-free header to avoid a circular include;
     * the values below use the same 0..4 IDLE/RECORDING/PLAYING/OVERDUB/
     * PAUSED numbering), one slot per track. LP_LOOP_TRACK_COUNT is
     * redefined here (rather than included) for the same circular-include
     * reason -- must stay equal to launchpad_controller.h's own constant. */
#define LP_LOOP_TRACK_COUNT 4
    int loop_state[LP_LOOP_TRACK_COUNT];
    int loop_count[LP_LOOP_TRACK_COUNT];
    uint32_t loop_duration_ms[LP_LOOP_TRACK_COUNT];
    uint32_t loop_pos_ms[LP_LOOP_TRACK_COUNT];
    int loop_selected;  /* which track Record/Play/Clear currently target */
    int loop_bpm;       /* visual metronome only -- see loop_bpm's comment in launchpad_controller.h */
    int loop_beat_pulse; /* 0-255, decays from 255 at the top of each beat; free-running, not tied to any track's own playback phase */
} lp_grid_model_t;
typedef struct { int x,y,w,h; } lp_rect_t;
/* Geometry and hit testing share the same rectangles. Controller is landscape. */
lp_rect_t lp_grid_area(int w,int h);
lp_rect_t lp_mode_tab_rect(int w,int h,int index);
/* Full redraw without present; required by the alternating framebuffers. */
void lp_draw_frame(sgfx_device_t*,int w,int h,const lp_grid_model_t*);
int lp_grid_hit_pad(int w,int h,lp_mode_t,int variant,int x,int y);
int lp_mode_tab_hit(int w,int h,int x,int y);
int lp_home_hit(int w,int h,int x,int y);
int lp_route_hit(int w,int h,int x,int y);
int lp_panic_hit(int w,int h,int x,int y);
/* Only evaluate channel cells while model.route_open is true. */
int lp_channel_cell_hit(int w,int h,int x,int y);
int lp_dim_hit(int w,int h,int x,int y);
int lp_mixer_col_hit(int w,int h,int variant,int x,int y);
/* Looper transport buttons -- live regardless of which mode tab is
 * currently showing, same reasoning as Panic (lp_panic_hit above): you
 * shouldn't have to be on the Looper tab for these to work, though in
 * practice they're only ever drawn/reachable there (see launchpad_grid.c's
 * looper() renderer). */
int lp_loop_rec_hit(int w,int h,int x,int y);
int lp_loop_playstop_hit(int w,int h,int x,int y);
int lp_loop_clear_hit(int w,int h,int x,int y);
/* Track selector row: returns 0..LP_LOOP_TRACK_COUNT-1, or -1 if the touch
 * missed every track button. Same "live regardless of current tab" caveat
 * as the transport buttons above. */
int lp_loop_track_hit(int w,int h,int x,int y);
int lp_loop_bpm_down_hit(int w,int h,int x,int y);
int lp_loop_bpm_up_hit(int w,int h,int x,int y);
#ifdef __cplusplus
}
#endif
