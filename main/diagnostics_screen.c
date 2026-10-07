#include "diagnostics_screen.h"
#include "orion.h"
#include "studio_ui.h"
#include "usb_midi_device.h"
#include "sic/sic.h"
#include "sic/sic_registry.h"
#include "sic/storage/sd.h"
#include "sic/power/battery.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#define MAX_ROWS 48
static int collect(su_info_row*rows) {
    int n=0;
    esp_chip_info_t chip;
    esp_chip_info(&chip);
#define ROW(name,...) do{if(n<MAX_ROWS){snprintf(rows[n].label,sizeof rows[n].label,"%s",name);snprintf(rows[n].value,sizeof rows[n].value,__VA_ARGS__);n++;}}while(0)
    ROW("Processor","ESP32-P4 / rev %d.%d / %d cores",chip.revision/100,chip.revision%100,chip.cores);
    ROW("Internal free","%u KB",(unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024));
    ROW("PSRAM free","%u KB",(unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)/1024));
    sic_battery_t bat;
    if(sic_battery_read(&bat)==0) {
        ROW("Battery","%d%% / %.2f V / %.0f mA",bat.percent,bat.voltage_v,bat.current_ma);
    }
    else {
        ROW("Battery","Unavailable");
    }
    const sd_t*sd=sic_sd(0);
    int present=sd&&sd->v&&sd->v->present&&sd->v->present(sd)==1;
    ROW("SD card","%s",present?"Present":"Not present");
    ROW("Display","%s / %d x %d",orion_panel_name(),orion_display_w(),orion_display_h());
    ROW("MIDI transport","%s",orion_usb_midi_ready()?"USB device active":"Unavailable");
    ROW("Host","%s",orion_usb_midi_host_connected()?"USB enumerated / DAW unverified":"Disconnected");
    for(int f=0; f<(int)SIC_F__COUNT; f++) {
        sic_func_id_t fn=(sic_func_id_t)f;
        if(sic_count_fn(fn)>0) {
            ROW(sic_func_name(fn),"%s",sic_name_fn(fn,0));
        }
    }
#undef ROW
    return n;
}
static int hit(int w,int h,int x,int y) {
    if(su_contains(su_back_rect(w,h),x,y))return 0;
    for(int i=0; i<2; i++)if(su_contains(su_page_rect(w,h,i),x,y))return i+1;
    return -1;
}
int run_diagnostics_screen(sgfx_device_t*d,const touch_t*t,struct konsole*ks) {
    (void)ks;
    static su_info_row rows[MAX_ROWS];
    int count=collect(rows),page=0,dirty=1,rotation=-1,was_down=0,pressed=-1,lx=0,ly=0;
    uint32_t refresh=orion_millis();
    for(; ; ) {
        if(orion_console_getch()>=0)break;
        orion_display_poll_auto_rotation();
        int w=orion_display_w(),h=orion_display_h(),r=orion_display_get_rotation();
        if(r!=rotation) {
            rotation=r;
            page=0;
            pressed=-1;
            dirty=1;
        }
        uint32_t now=orion_millis();
        if(now-refresh>=1000) {
            count=collect(rows);
            refresh=now;
            dirty=1;
        }
        int pages=(count+su_info_page_size(h)-1)/su_info_page_size(h);
        if(page>=pages)page=0;
        if(dirty) {
            su_diagnostics_draw(d,w,h,rows,count,page,orion_usb_midi_ready(),orion_usb_midi_host_connected());
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
            orion_display_phys_to_logical(pts[0].x,pts[0].y,&lx,&ly);
            if(!was_down)pressed=hit(w,h,lx,ly);
        }
        if(!down&&was_down&&pressed>=0&&pressed==hit(w,h,lx,ly)) {
            if(pressed==0)break;
            page=(page+(pressed==1?pages-1:1))%pages;
            dirty=1;
            pressed=-1;
        }
        was_down=down;
        orion_delay_ms(1);
    }
    return 0;
}
