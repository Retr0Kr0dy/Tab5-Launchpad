#pragma once
#include "launchpad_grid.h"
#define LP_TOUCH_CAP 10
typedef struct  {
    int x,y;
    uint8_t id;
}
lp_contact;
typedef int (*lp_send_fn)(void*,const uint8_t[3]);
typedef struct  {
    int active,id,target,anchor_y,anchor_value,last_x,last_y;
}
lp_owner;
/* Looper: records whatever send() actually transmits (any mode, not just
 * LP_MODE_LOOPER's own screen -- recording/playback state is global
 * controller state, independent of which tab is currently showing, same
 * as channels[]/variants[] above), PLUS whatever arrives from the host
 * (see lp_controller_loop_record_host() -- a separate entry point,
 * platform-specific code polls orion_usb_midi_recv() and feeds it in;
 * this header stays platform-free, it doesn't call the transport itself).
 * LP_LOOP_TRACK_COUNT independent tracks, each with its own buffer/state/
 * length -- NOT forced to share a duration (no beat-quantization in this
 * version; the BPM below is a visual metronome aid only, it does not snap
 * recording/playback timing to a grid). Only the "selected" track is
 * targeted by Record/Play/Clear at any time; unselected tracks keep
 * playing/recording in the background regardless of selection, same as
 * recording already kept working in the background regardless of which
 * mode TAB was showing in the single-track version this replaces.
 * LP_LOOP_MAX_EVENTS * sizeof(lp_loop_event_t) * LP_LOOP_TRACK_COUNT is
 * ~57KB total -- trivial against this chip's available RAM. */
#define LP_LOOP_MAX_EVENTS 2048
#define LP_LOOP_TRACK_COUNT 4
typedef struct { uint32_t offset_ms; uint8_t packet[3]; } lp_loop_event_t;
typedef enum {
    LP_LOOP_IDLE = 0,   /* nothing recorded on this track */
    LP_LOOP_RECORDING,  /* capturing this track's first pass; its length = whenever this ends */
    LP_LOOP_PLAYING,    /* looping playback, not currently recording, on this track */
    LP_LOOP_OVERDUB,    /* looping playback AND capturing additional events, on this track */
    LP_LOOP_PAUSED      /* this track has recorded content, playback stopped */
} lp_loop_state_t;

typedef struct {
    lp_loop_event_t events[LP_LOOP_MAX_EVENTS];
    int count;
    lp_loop_state_t state;
    uint32_t record_start;  /* ms timestamp RECORDING/OVERDUB started counting offsets from */
    uint32_t duration_ms;   /* set when this track's first RECORDING pass ends; fixed after that */
    uint32_t play_start;    /* ms timestamp this track's current playback pass 0-offset began */
    uint32_t last_pos;      /* this track's loop-relative ms position as of the previous tick */
} lp_loop_track_t;

typedef struct  {
    lp_grid_model_t model;
    lp_owner owners[LP_TOUCH_CAP];
    uint8_t channels[LP_MODE_COUNT],variants[LP_MODE_COUNT];
    uint8_t fader_values[16][LP_MIXER_MAX_COLS],macro_values[16][16];
    uint16_t used_channels;
    int wait_release;
    uint32_t panic_until;
    lp_send_fn send;
    void*send_context;
    lp_loop_track_t loop_tracks[LP_LOOP_TRACK_COUNT];
    int loop_selected;    /* 0..LP_LOOP_TRACK_COUNT-1; which track REC/Play/Clear target */
    int loop_bpm;         /* visual metronome pulse only -- never affects recording/playback timing */
    uint32_t now_cache;   /* set once per lp_controller_step() call; see send()'s own comment for why */
}
lp_controller;
void lp_controller_init(lp_controller*,lp_send_fn,void*);
/* Processes a complete current contact set. Returns 1 on Home. */
int lp_controller_step(lp_controller*,const lp_contact*,int,int,int,uint32_t);
/* Release tracked notes and quarantine current contacts until lifted. */
void lp_controller_release(lp_controller*);
void lp_controller_panic(lp_controller*,uint32_t);
/* REC button: applies to the SELECTED track. IDLE->RECORDING (start
 * capturing), RECORDING->PLAYING (close the loop -- duration = elapsed,
 * starts looping immediately), PLAYING->OVERDUB (keep looping, also
 * capture new events into it), OVERDUB->PLAYING (stop adding, keep
 * looping what's there). */
void lp_controller_loop_rec(lp_controller*,uint32_t now);
/* PLAY/STOP button: applies to the SELECTED track, toggles PLAYING/
 * OVERDUB <-> PAUSED. No-op from IDLE/RECORDING (nothing to play yet /
 * still defining the loop). */
void lp_controller_loop_playstop(lp_controller*,uint32_t now);
/* Wipes the SELECTED track's recorded events and returns it to IDLE.
 * Other tracks are unaffected. */
void lp_controller_loop_clear(lp_controller*);
/* Selects which track 0..LP_LOOP_TRACK_COUNT-1 subsequent Record/Play/
 * Clear taps target. Does not stop/start/affect any track's own state --
 * purely which one the transport buttons are currently pointed at. */
void lp_controller_loop_select(lp_controller*,int track);
/* +1/-1 BPM, clamped to a sane range (visual pulse only, see loop_bpm's
 * own comment -- never affects recording/playback). */
void lp_controller_loop_bpm_adjust(lp_controller*,int delta);
/* Feeds one incoming (host-originated) raw 3-byte MIDI packet into
 * whichever track is currently RECORDING/OVERDUB, if any -- a no-op
 * otherwise. Platform-specific code (launchpad_app.c) is what actually
 * polls orion_usb_midi_recv() and calls this; this header/implementation
 * stays platform-free like the rest of launchpad_controller.c. Captured
 * host messages are recorded only -- never echoed back to the host
 * immediately (that would just be a pointless echo); they're heard again
 * only when this track's loop plays back, same as locally-generated
 * events. */
void lp_controller_loop_record_host(lp_controller*,uint32_t now,const uint8_t packet[3]);
