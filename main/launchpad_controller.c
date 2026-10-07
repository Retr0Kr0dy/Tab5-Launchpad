#include "launchpad_controller.h"
#include <string.h>
#define IGNORED (-1)
#define XY_OWNER 100
#define FADER_OWNER 200
static int clamp(int v,int lo,int hi) {
    return v<lo?lo:v>hi?hi:v;
}
/* Loop event recording needs "now", but send() is called from many sites
 * that don't have it (press_pad-equivalent code, mixer/knob/xy handlers,
 * panic). Rather than threading a `now` parameter through every one of
 * them, lp_controller_step() (the only entry point that's actually ever
 * called with a fresh timestamp) caches it here once per call; send()
 * reads the cache. Always fresh enough -- nothing recording-relevant ever
 * happens outside a lp_controller_step() call.
 *
 * Takes the specific track rather than "the selected track": send()
 * records into EVERY track currently RECORDING/OVERDUB, not just whichever
 * one Record/Play/Clear happen to be pointed at right now -- lets you
 * arm overdub on track 2, flip the selector to track 3 to arm it too, and
 * have one played note land in both, same way the original single-track
 * version let you switch mode TABS mid-recording without losing capture. */
static void loop_close_full_track(lp_loop_track_t*t,uint32_t now_cache) {
    /* Reaching the fixed buffer limit is a valid automatic end-of-pass, not
     * an error. The old code merely changed RECORDING -> PLAYING and left
     * duration_ms at zero, producing a track that looked closed but could
     * never actually loop. Finalize the first pass exactly like pressing REC
     * a second time; a full overdub simply stops adding and keeps playing. */
    if(t->state==LP_LOOP_RECORDING) {
        uint32_t dur=now_cache-t->record_start;
        t->duration_ms=dur<1?1:dur;
        t->play_start=now_cache;
        t->last_pos=0;
        t->state=LP_LOOP_PLAYING;
    } else if(t->state==LP_LOOP_OVERDUB) {
        t->state=LP_LOOP_PLAYING;
    }
}
static void loop_record_event(lp_loop_track_t*t,uint32_t now_cache,const uint8_t p[3]) {
    if(t->count>=LP_LOOP_MAX_EVENTS) {
        loop_close_full_track(t,now_cache);
        return;
    }
    /* RECORDING (the first, loop-length-defining pass): offset relative to
     * when recording started. OVERDUB (adding to an already-looping
     * buffer): offset relative to the loop's own playback 0-point,
     * wrapped by duration -- aligns the new event with where the
     * already-playing loop actually is right now, regardless of when
     * overdub was toggled on, not with whenever this overdub pass itself
     * began. */
    uint32_t offset=(t->state==LP_LOOP_OVERDUB&&t->duration_ms>0)
        ?(now_cache-t->play_start)%t->duration_ms
        :(now_cache-t->record_start);
    lp_loop_event_t*e=&t->events[t->count++];
    e->offset_ms=offset;
    memcpy(e->packet,p,3);
    /* Close immediately when the final slot is consumed. Waiting for a 2049th
     * event made the full-buffer behavior depend on whether another message
     * happened to arrive. */
    if(t->count>=LP_LOOP_MAX_EVENTS)loop_close_full_track(t,now_cache);
}

static void send(lp_controller*c,lp_midi_msg_t m) {
    uint8_t status=m.kind==LP_MSG_CC?0xB0:m.kind==LP_MSG_NOTE_ON?0x90:0x80;
    uint8_t p[3]= {
        status|c->model.channel,m.number,m.value
    };
    c->used_channels|=(uint16_t)(1u<<c->model.channel);
    /* A later successful send must clear a stale failure, not just Panic --
     * otherwise one dropped message while the host isn't attached yet (e.g.
     * powering on before the USB-A cable is plugged in) leaves "Send
     * failed" stuck forever even once every message after it succeeds. */
    if(c->send(c->send_context,p)!=0)c->model.tx_failed=1;
    else c->model.tx_failed=0;
    /* Capture whatever actually got sent, from ANY mode -- not just
     * LP_MODE_LOOPER's own screen. Recording/playback is global
     * controller state, same as channels[]/variants[] above: you can
     * switch to Looper, hit record, switch to Notes, play a part, and the
     * loop still captured it. Playback itself calls c->send() directly
     * (see loop_tick() below), bypassing this function entirely, so a
     * played-back event is never re-recorded into itself during overdub.
     * Checks every track, not just the selected one -- see
     * loop_record_event()'s own comment for why. */
    for(int t=0; t<LP_LOOP_TRACK_COUNT; t++) {
        lp_loop_track_t*tr=&c->loop_tracks[t];
        if(tr->state==LP_LOOP_RECORDING||tr->state==LP_LOOP_OVERDUB) {
            loop_record_event(tr,c->now_cache,p);
        }
    }
}
/* Reserved host-originated capture hook: same per-track fan-out as
 * send()'s recording hook, but never transmitted immediately. The release
 * hardware path does not call this yet; it remains here so the future
 * CIN/message-length-aware USB-MIDI receive implementation can feed the
 * platform-free looper without redesigning it (and so native tests can
 * exercise recording without generating thousands of local touch events). */
void lp_controller_loop_record_host(lp_controller*c,uint32_t now,const uint8_t packet[3]) {
    for(int t=0; t<LP_LOOP_TRACK_COUNT; t++) {
        lp_loop_track_t*tr=&c->loop_tracks[t];
        if(tr->state==LP_LOOP_RECORDING||tr->state==LP_LOOP_OVERDUB) {
            loop_record_event(tr,now,packet);
        }
    }
}
static void release_owner(lp_controller*c,lp_owner*o) {
    if(o->target>=0&&o->target<LP_MAX_PADS) {
        int pad=o->target;
        if(c->model.pad_down[pad]) {
            lp_msg_result_t r=lp_mode_pad_event(c->model.mode,c->model.variant,pad,0);
            if(r.valid)send(c,r.msg);
        }
        c->model.pad_down[pad]=0;
    }
    o->active=0;
    o->target=IGNORED;
}
void lp_controller_release(lp_controller*c) {
    for(int i=0; i<LP_TOUCH_CAP; i++)release_owner(c,&c->owners[i]);
    memset(c->model.mixer_touch_active,0,sizeof c->model.mixer_touch_active);
    c->model.xy_active=0;
    c->wait_release=1;
}
static void load_values(lp_controller*c) {
    memcpy(c->model.mixer_value,c->fader_values[c->model.channel],LP_MIXER_MAX_COLS);
    memcpy(c->model.knob_value,c->macro_values[c->model.channel],16);
    c->model.xy_valid=0;
}
void lp_controller_init(lp_controller*c,lp_send_fn fn,void*ctx) {
    memset(c,0,sizeof *c);
    c->channels[LP_MODE_DRUM]=9;
    c->loop_bpm=120; /* visual metronome default -- see loop_bpm's own comment */
    c->send=fn;
    c->send_context=ctx;
}
void lp_controller_panic(lp_controller*c,uint32_t now) {
    c->model.tx_failed=0;
    lp_controller_release(c);
    uint8_t saved=c->model.channel;
    uint16_t channels=c->used_channels|(1u<<saved);
    for(int ch=0; ch<16; ch++)if(channels&(1u<<ch)) {
        c->model.channel=ch;
        send(c,(lp_midi_msg_t) {
            LP_MSG_CC,64,0
        });
        send(c,(lp_midi_msg_t) {
            LP_MSG_CC,123,0
        });
        send(c,(lp_midi_msg_t) {
            LP_MSG_CC,120,0
        });
    }
    c->model.channel=saved;
    c->model.panic_sent=!c->model.tx_failed;
    c->panic_until=now+900;
}
static int loop_note_active(const lp_loop_track_t*t,int ch,int note) {
    return (t->active_notes[ch][note>>3]>>(note&7))&1;
}
static void loop_set_note_active(lp_loop_track_t*t,int ch,int note,int active) {
    uint8_t mask=(uint8_t)(1u<<(note&7));
    if(active)t->active_notes[ch][note>>3]|=mask;
    else t->active_notes[ch][note>>3]&=(uint8_t)~mask;
}
static void loop_note_state_after_success(lp_loop_track_t*t,const uint8_t p[3]) {
    uint8_t type=p[0]&0xF0;
    if(type!=0x80&&type!=0x90)return;
    int ch=p[0]&0x0F,note=p[1]&0x7F;
    if(type==0x90&&p[2]!=0)loop_set_note_active(t,ch,note,1);
    else loop_set_note_active(t,ch,note,0); /* Note Off or Note On velocity 0 */
}
static void loop_send_playback(lp_controller*c,lp_loop_track_t*t,const uint8_t p[3]) {
    int rc=c->send(c->send_context,p);
    if(rc!=0)c->model.tx_failed=1;
    else {
        c->model.tx_failed=0;
        if((p[0]&0xF0)>=0x80&&(p[0]&0xF0)<=0xE0)c->used_channels|=(uint16_t)(1u<<(p[0]&0x0F));
        loop_note_state_after_success(t,p);
    }
}
static void loop_flush_active_notes(lp_controller*c,lp_loop_track_t*t) {
    /* Pausing/clearing halfway between a looped Note On and Note Off used to
     * strand the note forever. Release every note this track has actually
     * turned on. Failed sends stay marked active so subsequent idle/paused
     * ticks can retry after a transient disconnect/FIFO-full condition. */
    for(int ch=0; ch<16; ch++)for(int note=0; note<128; note++)if(loop_note_active(t,ch,note)) {
        uint8_t p[3]={(uint8_t)(0x80|ch),(uint8_t)note,0};
        c->used_channels|=(uint16_t)(1u<<ch);
        if(c->send(c->send_context,p)==0) {
            c->model.tx_failed=0;
            loop_set_note_active(t,ch,note,0);
        } else c->model.tx_failed=1;
    }
}
/* Runs every track independently -- each has its own duration/phase, so
 * e.g. track 0 can be a 4-beat loop and track 1 an 8-beat loop both
 * playing at once, same as a real hardware multitrack looper (no forced
 * common bar length in this version). */
static void loop_tick_track(lp_controller*c,lp_loop_track_t*t,int idx,uint32_t now) {
    c->model.loop_state[idx]=(int)t->state;
    c->model.loop_count[idx]=t->count;
    c->model.loop_duration_ms[idx]=t->duration_ms;
    if(t->state!=LP_LOOP_PLAYING&&t->state!=LP_LOOP_OVERDUB) {
        /* Normally there is nothing left after Pause/Clear, but if a Note Off
         * could not be delivered (temporary transport failure), retry it on
         * later ticks rather than forgetting ownership and leaving a synth
         * hanging forever. */
        loop_flush_active_notes(c,t);
        return;
    }
    if(t->duration_ms==0) {
        c->model.loop_pos_ms[idx]=0;
        return;
    }
    uint32_t pos=(now-t->play_start)%t->duration_ms;
    c->model.loop_pos_ms[idx]=pos;
    uint32_t last=t->last_pos;
    /* Due window is (last,pos] -- or, if this tick crossed the loop
     * boundary (pos < last), the union of (last,duration] and [0,pos].
     * A linear scan every tick is trivial at this event-count scale (at
     * most LP_LOOP_MAX_EVENTS, checked once per ~ms poll tick) -- no need
     * for a sorted/indexed walk, which would also be awkward given
     * overdub can append events out of a single pass's chronological
     * order relative to the buffer as a whole. */
    int wrapped=pos<last;
    for(int i=0; i<t->count; i++) {
        uint32_t off=t->events[i].offset_ms;
        int due=wrapped?(off>last||off<=pos):(off>last&&off<=pos);
        if(due) {
            /* Raw send, NOT the recording send() above -- a played-back
             * event must never re-record itself into the buffer during
             * overdub (infinite/duplicating growth). */
            loop_send_playback(c,t,t->events[i].packet);
        }
    }
    t->last_pos=pos;
}
static void loop_tick(lp_controller*c,uint32_t now) {
    for(int i=0; i<LP_LOOP_TRACK_COUNT; i++)loop_tick_track(c,&c->loop_tracks[i],i,now);
    c->model.loop_selected=c->loop_selected;
    c->model.loop_bpm=c->loop_bpm;
    /* Free-running visual metronome, independent of any track's own
     * recording/playback phase (see loop_bpm's comment in the header --
     * purely a tempo-feel aid, never used to quantize or align anything).
     * A short 80ms decay flash at the top of every beat; anchored at
     * `now`'s own epoch (not any recording start) since nothing needs it
     * phase-locked to a track. */
    uint32_t period=60000u/(uint32_t)(c->loop_bpm>0?c->loop_bpm:120);
    uint32_t phase=period?now%period:0;
    c->model.loop_beat_pulse=phase<80?(int)(255-(255*phase)/80):0;
}
void lp_controller_loop_rec(lp_controller*c,uint32_t now) {
    lp_loop_track_t*t=&c->loop_tracks[c->loop_selected];
    switch(t->state) {
    case LP_LOOP_IDLE:
        t->count=0;
        t->record_start=now;
        t->state=LP_LOOP_RECORDING;
        break;
    case LP_LOOP_RECORDING: {
        uint32_t dur=now-t->record_start;
        t->duration_ms=dur<1?1:dur; /* never 0 -- avoids a div-by-zero in loop_tick()/loop_record_event() */
        t->play_start=now;
        t->last_pos=0;
        t->state=LP_LOOP_PLAYING;
        break;
    }
    case LP_LOOP_PLAYING:
        t->state=LP_LOOP_OVERDUB;
        break;
    case LP_LOOP_OVERDUB:
        t->state=LP_LOOP_PLAYING;
        break;
    case LP_LOOP_PAUSED:
        /* Resume exactly where it was paused (not reset to 0), now also recording. */
        t->play_start=now-t->last_pos;
        t->state=LP_LOOP_OVERDUB;
        break;
    }
}
void lp_controller_loop_playstop(lp_controller*c,uint32_t now) {
    lp_loop_track_t*t=&c->loop_tracks[c->loop_selected];
    if(t->state==LP_LOOP_PLAYING||t->state==LP_LOOP_OVERDUB) {
        loop_flush_active_notes(c,t);
        t->state=LP_LOOP_PAUSED;
    } else if(t->state==LP_LOOP_PAUSED) {
        t->play_start=now-t->last_pos; /* resume, don't restart */
        t->state=LP_LOOP_PLAYING;
    }
    /* IDLE/RECORDING: no-op -- nothing to play yet / still defining the loop. */
}
void lp_controller_loop_clear(lp_controller*c) {
    lp_loop_track_t*t=&c->loop_tracks[c->loop_selected];
    loop_flush_active_notes(c,t);
    t->count=0;
    t->duration_ms=0;
    t->state=LP_LOOP_IDLE;
}
void lp_controller_loop_select(lp_controller*c,int track) {
    if(track>=0&&track<LP_LOOP_TRACK_COUNT)c->loop_selected=track;
}
void lp_controller_loop_bpm_adjust(lp_controller*c,int delta) {
    c->loop_bpm=clamp(c->loop_bpm+delta,20,300);
}
static int owned(lp_controller*c,int target) {
    for(int i=0; i<LP_TOUCH_CAP; i++)if(c->owners[i].active&&c->owners[i].target==target)return 1;
    return 0;
}
int lp_controller_step(lp_controller*c,const lp_contact*p,int n,int w,int h,uint32_t now) {
    lp_grid_model_t*m=&c->model;
    c->now_cache=now;
    /* Loop playback is background/timer-driven, independent of touch
     * state -- must run even during a wait_release quarantine window
     * (e.g. right after the touch that switched tabs into Looper), so
     * this runs before that early-return below, not after. */
    loop_tick(c,now);
    if(m->panic_sent&&(int32_t)(now-c->panic_until)>=0)m->panic_sent=0;
    n=clamp(n,0,LP_TOUCH_CAP);
    if(c->wait_release) {
        if(n==0)c->wait_release=0;
        return 0;
    }
    /* Reconcile releases before processing new contacts (touch IDs may be reused). */
    for(int s=0; s<LP_TOUCH_CAP; s++)if(c->owners[s].active) {
        int found=0;
        for(int i=0; i<n; i++)if(p[i].id==c->owners[s].id)found=1;
        if(!found)release_owner(c,&c->owners[s]);
    }
    m->xy_active=0;
    memset(m->mixer_touch_active,0,sizeof m->mixer_touch_active);
    for(int i=0; i<n; i++) {
        int s=-1;
        for(int j=0; j<LP_TOUCH_CAP; j++)if(c->owners[j].active&&c->owners[j].id==p[i].id) {
            s=j;
            break;
        }
        int fresh=s<0;
        if(fresh) {
            for(int j=0; j<LP_TOUCH_CAP; j++)if(!c->owners[j].active) {
                s=j;
                break;
            }
            if(s<0)continue;
            c->owners[s]=(lp_owner) {
                .active=1,.id=p[i].id,.target=IGNORED,.last_x=-1,.last_y=-1
            };
        }
        lp_owner*o=&c->owners[s];
        int x=p[i].x,y=p[i].y;
        if(m->route_open) {
            if(fresh) {
                int ch=lp_channel_cell_hit(w,h,x,y);
                if(ch>=0) {
                    lp_controller_release(c);
                    m->channel=ch;
                    c->channels[m->mode]=ch;
                    load_values(c);
                    m->route_open=0;
                    return 0;
                }
                if(lp_home_hit(w,h,x,y)) {
                    m->route_open=0;
                    lp_controller_release(c);
                    return 0;
                }
            }
            continue;
        }
        if(fresh) {
            if(lp_panic_hit(w,h,x,y)) {
                lp_controller_panic(c,now);
                return 0;
            }
            int tab=lp_mode_tab_hit(w,h,x,y);
            if(tab>=0) {
                if(tab!=(int)m->mode) {
                    lp_controller_release(c);
                    m->mode=tab;
                    m->channel=c->channels[tab];
                    m->variant=c->variants[tab];
                    load_values(c);
                    return 0;
                }
                continue;
            }
            if(lp_home_hit(w,h,x,y)) {
                lp_controller_release(c);
                return 1;
            }
            if(lp_route_hit(w,h,x,y)) {
                lp_controller_release(c);
                m->route_open=1;
                return 0;
            }
            if(lp_dim_hit(w,h,x,y)) {
                int count=lp_mode_variant_count(m->mode);
                if(count>1) {
                    lp_controller_release(c);
                    m->variant=(m->variant+1)%count;
                    c->variants[m->mode]=m->variant;
                    return 0;
                }
                continue;
            }
            /* Gated on LP_MODE_LOOPER: these buttons only ever get drawn
             * there (launchpad_grid.c's looper()), but their hit-rects are
             * still at fixed screen coordinates like every other footer
             * control -- without this gate, a tap at that same spot on a
             * DIFFERENT tab (landing on a pad/fader/knob cell instead)
             * would be misinterpreted as a loop transport press. */
            if(m->mode==LP_MODE_LOOPER) {
                int tr=lp_loop_track_hit(w,h,x,y);
                if(tr>=0) {
                    lp_controller_loop_select(c,tr);
                    continue;
                }
                if(lp_loop_rec_hit(w,h,x,y)) {
                    lp_controller_loop_rec(c,now);
                    continue;
                }
                if(lp_loop_playstop_hit(w,h,x,y)) {
                    lp_controller_loop_playstop(c,now);
                    continue;
                }
                if(lp_loop_clear_hit(w,h,x,y)) {
                    lp_controller_loop_clear(c);
                    continue;
                }
                if(lp_loop_bpm_down_hit(w,h,x,y)) {
                    lp_controller_loop_bpm_adjust(c,-1);
                    continue;
                }
                if(lp_loop_bpm_up_hit(w,h,x,y)) {
                    lp_controller_loop_bpm_adjust(c,1);
                    continue;
                }
            }
            lp_rect_t a=lp_grid_area(w,h);
            int target=IGNORED;
            if(m->mode==LP_MODE_MIXER_CC) {
                int col=lp_mixer_col_hit(w,h,m->variant,x,y);
                if(col>=0)target=FADER_OWNER+col;
            } else if(m->mode==LP_MODE_XY_MACRO) {
                if(x>=a.x&&y>=a.y&&x<a.x+a.w&&y<a.y+a.h)target=XY_OWNER;
            } else target=lp_grid_hit_pad(w,h,m->mode,m->variant,x,y);
            if(target>=0&&!owned(c,target)) {
                o->target=target;
                o->anchor_y=y;
                if(target>=FADER_OWNER)o->anchor_value=m->mixer_value[target-FADER_OWNER];
                else if(m->mode==LP_MODE_KNOB)o->anchor_value=m->knob_value[target];
                else if(target<LP_MAX_PADS) {
                    m->pad_down[target]=1;
                    lp_msg_result_t r=lp_mode_pad_event(m->mode,m->variant,target,1);
                    if(r.valid)send(c,r.msg);
                }
            }
        }
        if(o->target==IGNORED)continue;
        if(m->mode==LP_MODE_MIXER_CC&&o->target>=FADER_OWNER) {
            int col=o->target-FADER_OWNER;
            lp_rect_t a=lp_grid_area(w,h);
            int sweep=a.h-143;
            if(sweep<100)sweep=100;
            int v=clamp(o->anchor_value+(o->anchor_y-y)*127/sweep,0,127);
            m->mixer_touch_active[col]=1;
            if(v!=m->mixer_value[col]) {
                m->mixer_value[col]=v;
                c->fader_values[m->channel][col]=v;
                send(c,lp_mode_mixer_event(m->variant,col,v));
            }
        } else if(m->mode==LP_MODE_KNOB&&o->target>=0&&o->target<16) {
            int knob=o->target;
            int v=clamp(o->anchor_value+(o->anchor_y-y)*127/240,0,127);
            if(v!=m->knob_value[knob]) {
                m->knob_value[knob]=v;
                c->macro_values[m->channel][knob]=v;
                send(c,lp_mode_knob_event(knob,v));
            }
        } else if(m->mode==LP_MODE_XY_MACRO&&o->target==XY_OWNER) {
            lp_rect_t a=lp_grid_area(w,h);
            x=clamp(x,a.x,a.x+a.w-1);
            y=clamp(y,a.y,a.y+a.h-1);
            m->xy_active=1;
            m->xy_valid=1;
            m->xy_x=x;
            m->xy_y=y;
            int nx=(x-a.x)*127/(a.w-1),ny=127-(y-a.y)*127/(a.h-1);
            lp_midi_msg_t msgs[2];
            lp_mode_xy_event(nx,ny,msgs);
            if(nx!=o->last_x)send(c,msgs[0]);
            if(ny!=o->last_y)send(c,msgs[1]);
            o->last_x=nx;
            o->last_y=ny;
        }
    }
    return 0;
}
