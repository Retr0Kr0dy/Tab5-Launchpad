#include "settings_screen.h"
#include "orion.h"
#include "studio_ui.h"
#include "usb_midi_device.h"
#include "settings_store.h"
#include "debug_overlay.h"
#include <stdio.h>
static void draw(sgfx_device_t*d,int w,int h) {
    char panel[96],build[80];
    snprintf(panel,sizeof panel,"Display / %s / %d x %d",orion_panel_name(),w,h);
    snprintf(build,sizeof build,"Firmware %s / %s",ORION_VERSION,__DATE__);
    su_settings_draw(d,w,h,orion_backlight_get(),orion_display_auto_rotation_enabled(),debug_overlay_enabled(),orion_usb_midi_ready(),orion_usb_midi_host_connected(),build,panel);
    sgfx_present(d);
}
static int hit(int w,int h,int x,int y) {
    for(int i=0; i<3; i++)if(su_contains(su_setting_rect(w,h,i),x,y))return i;
    return su_contains(su_back_rect(w,h),x,y)?3:-1;
}
int run_settings_screen(sgfx_device_t*d,const touch_t*t,struct konsole*ks) {
    (void)ks;
    int was_down=0,pressed=-1,lx=0,ly=0,rotation=-1,dirty=1,save=0,last_host=-1,last_ready=-1;
    uint32_t last_draw=0;
    for(; ; ) {
        if(orion_console_getch()>=0)break;
        orion_display_poll_auto_rotation();
        int w=orion_display_w(),h=orion_display_h(),r=orion_display_get_rotation();
        if(rotation!=r) {
            rotation=r;
            pressed=-1;
            dirty=1;
        }
        int host=orion_usb_midi_host_connected(),ready=orion_usb_midi_ready();
        if(host!=last_host||ready!=last_ready) {
            last_host=host;
            last_ready=ready;
            dirty=1;
        }
        sic_touch_point_t pts[4];
        int n=t->v->read_points(t,pts,4);
        if(n<0) {
            orion_delay_ms(1);
            continue;
        }
        int down=n>0;
        if(down) {
            orion_display_phys_to_logical(pts[0].x,pts[0].y,&lx,&ly);
            if(!was_down)pressed=hit(w,h,lx,ly);
        }
        if(down&&pressed==0) {
            su_rect b=su_setting_rect(w,h,0);
            int x=b.x+b.w/2,width=b.w/2-120;
            int value=(lx-x)*100/width;
            if(value<1)value=1;
            if(value>100)value=100;
            if(value!=orion_backlight_get()) {
                orion_backlight_set(value);
                dirty=1;
                save=1;
            }
        }
        if(!down&&was_down) {
            if(pressed==0&&save) {
                orion_settings_save_sd();
                save=0;
            }
            else if(pressed>=0&&hit(w,h,lx,ly)==pressed) {
                if(pressed==1) {
                    orion_display_set_auto_rotation(!orion_display_auto_rotation_enabled());
                    orion_settings_save_sd();
                    dirty=1;
                }
                if(pressed==2) {
                    debug_overlay_set_enabled(!debug_overlay_enabled());
                    dirty=1;
                }
                if(pressed==3)break;
            }
            pressed=-1;
        }
        uint32_t now=orion_millis();
        if(dirty&&now-last_draw>=LP_PANEL_FRAME_MS) {
            draw(d,w,h);
            dirty=0;
            last_draw=now;
        }
        was_down=down;
        orion_delay_ms(1);
    }
    if(save)orion_settings_save_sd();
    return 0;
}
