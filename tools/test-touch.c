/* Drive the actual drivers with deterministic register replies. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../lib/SIC/src/drivers/input/touch_st712x.c"
#include "../lib/SIC/src/drivers/input/touch_gt911.c"
static int fresh=1,contacts=2,io_error;
void sic_registry_register(const sic_driver_t*d){(void)d;}
tab5_panel_kind_t tab5_panel_detected(void){return TAB5_PANEL_ST7121;}
int sic_i2c_write(int bus,uint8_t addr,const uint8_t*buf,int n){(void)bus;(void)addr;(void)buf;return io_error?-1:n;}
int sic_i2c_writeread(int bus,uint8_t addr,const uint8_t*wr,int nw,uint8_t*rd,int nr){
 (void)bus;(void)addr;(void)nw;if(io_error)return -1;memset(rd,0,nr);unsigned reg=(wr[0]<<8)|wr[1];
 if(reg==ST712X_REG_ADV_INFO)rd[0]=fresh?ST712X_ADV_WITH_COORD:0;
 else if(reg==ST712X_REG_MAX_TOUCHES)rd[0]=2;
 else if(reg==ST712X_REG_REPORT0){for(int i=0;i<contacts;i++){rd[i*7]=0x80;rd[i*7+1]=40+i;rd[i*7+3]=80+i;}}
 else if(reg==GT911_REG_STATUS)rd[0]=fresh?(GT911_STATUS_READY|contacts):0;
 else if(reg>=GT911_REG_POINT0){rd[0]=(reg-GT911_REG_POINT0)/8;rd[1]=40;rd[3]=80;}
 return nr;
}
int main(void){sic_touch_point_t p[10];st712x_ctx_t a={0};touch_t st={&ST712X_VT,&a};gt911_ctx_t b={0};touch_t gt={&GT911_VT,&b};touch_t*all[]={&st,&gt};
 for(int k=0;k<2;k++){touch_t*t=all[k];fresh=1;contacts=2;io_error=0;assert(t->v->read_points(t,p,1)==1);fresh=0;assert(t->v->read_points(t,p,10)==2);assert(p[0].x==40);io_error=1;assert(t->v->read_points(t,p,10)<0);io_error=0;assert(t->v->read_points(t,p,10)==2);fresh=1;contacts=0;assert(t->v->read_points(t,p,10)==0);fresh=0;assert(t->v->read_points(t,p,10)==0);}
 puts("Touch drivers: fresh report, held snapshot, caller capacity, I2C error and explicit release passed.");return 0;
}
