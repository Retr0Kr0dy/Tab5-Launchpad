#include "orion.h"
#include "studio_ui.h"
#include "launchpad_app.h"
#include "settings_screen.h"
#include "diagnostics_screen.h"
#include "usb_midi_device.h"
#include "sic/sic.h"
#include "sic/input/touch.h"
static int hit(int w,int h,int x,int y) {
    for(int i=0; i<3; i++)if(su_contains(su_home_tile(w,h,i),x,y))return i;
    return -1;
}
int orion_shell_run(struct konsole*ks) {
    sgfx_device_t*d=orion_gfx();
    const touch_t*t=sic_touch(0);
    if(!d||!t)return -1;
    int was_down=0,pressed=-1,last_x=0,last_y=0,rotation=-1,old_ready=-1,old_host=-1,dirty=1;
    for(; ; ) {
        konsole_poll(ks);
        orion_display_poll_auto_rotation();
        int r=orion_display_get_rotation();
        int w=orion_display_w(),h=orion_display_h();
        int ready=orion_usb_midi_ready(),host=orion_usb_midi_host_connected();
        if(r!=rotation) {
            rotation=r;
            pressed=-1;
            dirty=1;
        }
        if(ready!=old_ready||host!=old_host) {
            old_ready=ready;
            old_host=host;
            dirty=1;
        }
        if(dirty) {
            su_home_draw(d,w,h,ready,host);
            sgfx_present(d);
            dirty=0;
        }
        sic_touch_point_t pts[4];
        int n=t->v->read_points(t,pts,4);
        if(n<0) {
            orion_delay_ms(1);
            continue;
        }
        int down=n>0;
        if(down) {
            orion_display_phys_to_logical(pts[0].x,pts[0].y,&last_x,&last_y);
            if(!was_down)pressed=hit(w,h,last_x,last_y);
        }
        if(!down&&was_down) {
            int target=hit(w,h,last_x,last_y);
            if(pressed>=0&&target==pressed) {
                if(target==0)run_launchpad_app(d,t,ks);
                else if(target==1)run_settings_screen(d,t,ks);
                else run_diagnostics_screen(d,t,ks);
                dirty=1;
                pressed=-1;
            }
        }
        was_down=down;
        orion_delay_ms(1);
    }
}
