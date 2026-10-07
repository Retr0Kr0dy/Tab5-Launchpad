#include "launchpad_controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t messages[8192][3];
static int sent,fail;
static int capture(void*ctx,const uint8_t p[3]) {
    (void)ctx;
    assert(sent<(int)(sizeof messages/sizeof messages[0]));
    memcpy(messages[sent++],p,3);
    return fail?-1:0;
}
static void step_at(lp_controller*c,lp_contact*p,int n,uint32_t now) {
    lp_controller_step(c,p,n,1280,720,now);
}
static void step(lp_controller*c,lp_contact*p,int n){step_at(c,p,n,100);}
static void lift(lp_controller*c){step(c,NULL,0);}
static void tap(lp_controller*c,int x,int y){lp_contact p={x,y,9};step(c,&p,1);lift(c);}
static void mode(lp_controller*c,int mode){lp_rect_t r=lp_mode_tab_rect(1280,720,mode);tap(c,r.x+20,r.y+20);}
static void reset(lp_controller*c){sent=fail=0;lp_controller_init(c,capture,NULL);}

static void test_looper_full_buffer(void) {
    lp_controller c;
    uint8_t note[3]={0x90,60,100};
    reset(&c);
    lp_controller_loop_rec(&c,1000);
    assert(c.loop_tracks[0].state==LP_LOOP_RECORDING);
    for(int i=0;i<LP_LOOP_MAX_EVENTS;i++) {
        note[1]=(uint8_t)(i&0x7f);
        lp_controller_loop_record_host(&c,1001u+(uint32_t)i,note);
    }
    assert(c.loop_tracks[0].count==LP_LOOP_MAX_EVENTS);
    assert(c.loop_tracks[0].state==LP_LOOP_PLAYING);
    assert(c.loop_tracks[0].duration_ms>0);
    assert(c.loop_tracks[0].play_start==1000u+LP_LOOP_MAX_EVENTS);
}

static void build_note_loop(lp_controller*c) {
    uint8_t on[3]={0x90,60,100},off[3]={0x80,60,0};
    reset(c);
    lp_controller_loop_rec(c,1000);
    lp_controller_loop_record_host(c,1010,on);
    lp_controller_loop_record_host(c,1500,off);
    lp_controller_loop_rec(c,2000);
    assert(c->loop_tracks[0].state==LP_LOOP_PLAYING);
    assert(c->loop_tracks[0].duration_ms==1000);
}
static void test_looper_pause_releases_note(void) {
    lp_controller c;
    build_note_loop(&c);
    step_at(&c,NULL,0,2011); /* replays Note On at offset 10 */
    assert(sent==1&&messages[0][0]==0x90&&messages[0][1]==60);
    lp_controller_loop_playstop(&c,2011);
    assert(c.loop_tracks[0].state==LP_LOOP_PAUSED);
    assert(sent==2&&messages[1][0]==0x80&&messages[1][1]==60);
}
static void test_looper_clear_releases_note(void) {
    lp_controller c;
    build_note_loop(&c);
    step_at(&c,NULL,0,2011);
    assert(sent==1&&messages[0][0]==0x90&&messages[0][1]==60);
    lp_controller_loop_clear(&c);
    assert(c.loop_tracks[0].state==LP_LOOP_IDLE&&c.loop_tracks[0].count==0);
    assert(sent==2&&messages[1][0]==0x80&&messages[1][1]==60);
}

int main(void) {
    lp_controller c;
    lp_contact pad={40,210,1};
    lp_contact pair[2];

    reset(&c);
    step(&c,&pad,1);
    assert(sent==1&&messages[0][0]==0x90&&messages[0][1]==36);
    step(&c,&pad,1);
    assert(sent==1);
    lift(&c);
    assert(sent==2&&messages[1][0]==0x80);

    /* Changing route with notes held releases on the old channel. */
    reset(&c);
    step(&c,&pad,1);
    pair[0]=pad; pair[1]=(lp_contact){220,650,2};
    step(&c,pair,2);
    assert(c.model.route_open&&sent==2&&messages[1][0]==0x80);
    lift(&c);
    tap(&c,1000,510);
    assert(c.model.channel==15&&!c.model.route_open);
    step(&c,&pad,1); lift(&c);
    assert(messages[2][0]==0x9f&&messages[3][0]==0x8f);

    /* Mode recall does not leave notes or transfer held touches into new pads. */
    reset(&c);
    step(&c,&pad,1);
    lp_rect_t tab=lp_mode_tab_rect(1280,720,LP_MODE_DRUM);
    pair[0]=pad; pair[1]=(lp_contact){tab.x+10,tab.y+10,2};
    step(&c,pair,2);
    assert(sent==2&&messages[1][0]==0x80&&c.model.channel==9);
    step(&c,&pad,1);
    assert(sent==2);
    lift(&c);
    mode(&c,LP_MODE_NOTE);
    assert(c.model.channel==0);

    /* Multiple contacts cannot double-trigger one pad. */
    reset(&c);
    pair[0]=pad; pair[1]=(lp_contact){42,212,2};
    step(&c,pair,2);
    assert(sent==1);
    step(&c,pair+1,1);
    assert(sent==2);
    lift(&c);
    assert(sent==2);

    /* Faders start without a jump, and unchanged values do not flood USB. */
    reset(&c);
    mode(&c,LP_MODE_MIXER_CC);
    lp_contact f={100,480,1};
    step(&c,&f,1);
    assert(sent==0);
    f.y-=100;
    step(&c,&f,1);
    assert(sent==1&&messages[0][1]==20&&messages[0][2]>0);
    int value=c.model.mixer_value[0];
    step(&c,&f,1);
    assert(sent==1);
    lift(&c);
    mode(&c,LP_MODE_NOTE); mode(&c,LP_MODE_MIXER_CC);
    assert(c.model.mixer_value[0]==value);

    /* One XY owner, quantized deduplication, and hold on release. */
    reset(&c);
    mode(&c,LP_MODE_XY_MACRO);
    pair[0]=(lp_contact){100,250,1}; pair[1]=(lp_contact){1000,500,2};
    step(&c,pair,2);
    assert(sent==2&&c.model.xy_x==100);
    step(&c,pair,2);
    assert(sent==2);
    lift(&c);
    assert(!c.model.xy_active&&c.model.xy_valid);

    /* Layout change releases old mapping before changing dimensions. */
    reset(&c);
    step(&c,&pad,1);
    pair[0]=pad; pair[1]=(lp_contact){450,650,2};
    step(&c,pair,2);
    assert(sent==2&&messages[1][1]==36&&c.model.variant==1);

    /* Exit path and panic release notes; explicit send failures are visible. */
    reset(&c);
    step(&c,&pad,1);
    lp_controller_release(&c);
    assert(sent==2&&messages[1][0]==0x80);
    reset(&c);
    step(&c,&pad,1);
    lp_controller_panic(&c,100);
    assert(sent==5&&messages[1][0]==0x80&&messages[2][1]==64&&messages[3][1]==123&&messages[4][1]==120&&c.model.panic_sent);
    reset(&c);
    fail=1;
    step(&c,&pad,1);
    assert(c.model.tx_failed);
    lp_controller_panic(&c,100);
    assert(c.model.tx_failed&&!c.model.panic_sent);

    /* Every grid-based variant's rendered cell centers agree with its hit
     * tester. XY and Looper have dedicated non-grid controls and therefore
     * intentionally return -1 from lp_grid_hit_pad(). */
    for(int m=0;m<LP_MODE_COUNT;m++)for(int v=0;v<lp_mode_variant_count(m);v++) {
        lp_rect_t a=lp_grid_area(1280,720);
        lp_grid_shape_t s=lp_mode_grid_shape(m,v);
        int cw=(a.w-(s.cols-1)*10)/s.cols;
        int ch=(a.h-(s.rows-1)*10)/s.rows;
        for(int r=0;r<s.rows;r++)for(int col=0;col<s.cols;col++) {
            int x=a.x+col*(cw+10)+cw/2,y=a.y+r*(ch+10)+ch/2;
            if(m==LP_MODE_MIXER_CC)assert(lp_mixer_col_hit(1280,720,v,x,y)==col);
            else if(m!=LP_MODE_XY_MACRO&&m!=LP_MODE_LOOPER)
                assert(lp_grid_hit_pad(1280,720,m,v,x,y)==r*s.cols+col);
        }
    }

    test_looper_full_buffer();
    test_looper_pause_releases_note();
    test_looper_clear_releases_note();

    puts("Controller: lifecycle, routing, touch ownership, controls, panic, geometry and looper regressions passed.");
    return 0;
}
