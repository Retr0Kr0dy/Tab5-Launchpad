/* Platform adapter: the input/MIDI state machine is host-testable. */
#include "launchpad_app.h"
#include "launchpad_controller.h"
#include "usb_midi_device.h"
#include "debug_overlay.h"
#include "orion.h"
#include <string.h>
static int output(void*ctx,const uint8_t packet[3]) {
    (void)ctx;
    return orion_usb_midi_send(packet);
}
int run_launchpad_app(sgfx_device_t*d,const touch_t*t,struct konsole*ks) {
    (void)ks;
    int rotation=orion_display_get_rotation();
    orion_display_push_rotation(rotation==1||rotation==3?rotation:1);
    /* Recall each mode's channel, layout and local CC values until power off. */
    static lp_controller c;
    static int initialized;
    if(!initialized) {
        lp_controller_init(&c,output,NULL);
        initialized=1;
    }
    c.model.route_open=0;
    c.wait_release=1;
    lp_grid_model_t previous;
    int have_previous=0,old_host=orion_usb_midi_host_connected();
    uint32_t last_present=0,last_good=orion_millis();
    for(; ; ) {
        if(orion_console_getch()>=0)break;
        uint32_t now=orion_millis();
        int w=orion_display_w(),h=orion_display_h();
        int host=orion_usb_midi_host_connected();
        if(host!=old_host) {
            lp_controller_release(&c);
            old_host=host;
        }
        sic_touch_point_t points[LP_TOUCH_CAP];
        lp_contact contacts[LP_TOUCH_CAP];
        int n=t->v->read_points(t,points,LP_TOUCH_CAP);
        if(n>=0) {
            last_good=now;
            if(n>LP_TOUCH_CAP)n=LP_TOUCH_CAP;
            for(int i=0; i<n; i++) {
                orion_display_phys_to_logical(points[i].x,points[i].y,&contacts[i].x,&contacts[i].y);
                contacts[i].id=points[i].id;
            }
            if(lp_controller_step(&c,contacts,n,w,h,now))break;
        } else if(now-last_good>100) {
            lp_controller_release(&c);
            last_good=now;
        }
        /* Host -> device MIDI capture is intentionally disabled for this
         * release. The USB-MIDI receive primitive currently exposes only a
         * fixed three-byte payload and does not yet preserve/interpret the
         * USB-MIDI CIN/message length needed for all MIDI message classes.
         * Keep the portable looper hook for the future implementation, but
         * do not advertise or feed it from the hardware path until parsing
         * is complete. See README.md -> Planned post-release features. */
        c.model.midi_active=orion_usb_midi_ready();
        c.model.host_connected=host;
        int changed=!have_previous||memcmp(&previous,&c.model,sizeof previous)!=0;
        /* Full repaint even for overlay-only updates. Polling and drawing still
   * share this task: timing must be qualified on hardware, not inferred
   * from the nominal 1 ms delay. */
        int overlay_tick=debug_overlay_enabled()&&now-last_present>=150;
        if((changed||overlay_tick)&&(!have_previous||now-last_present>=LP_PANEL_FRAME_MS)) {
            lp_draw_frame(d,w,h,&c.model);
            if(debug_overlay_enabled()) {
                debug_overlay_frame();
                debug_overlay_draw(d,w,h);
            }
            sgfx_present(d);
            previous=c.model;
            have_previous=1;
            last_present=now;
        }
        orion_delay_ms(1);
    }
    lp_controller_release(&c);
    c.model.route_open=0;
    orion_display_pop_rotation();
    return 0;
}
