#if defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)
#include <Arduino.h>
#include <Wire.h>
#include "sic/bus/i2c_bus.h"
extern "C" {
int  sic_i2c_begin_bus(int bus, int sda, int scl, uint32_t hz){ (void)bus; Wire.begin(sda,scl,hz); return 0; }
int  sic_i2c_scan_bus(int bus, uint8_t* addrs, int max){
  (void)bus; int n=0;
  for (uint8_t a=1;a<127 && n<max;a++){
    Wire.beginTransmission(a);
    if (Wire.endTransmission()==0) addrs[n++]=a;
  }
  return n;
}
}

int  sic_i2c_write(int bus, uint8_t addr, const uint8_t* buf, int n){
  (void)bus; Wire.beginTransmission(addr);
  Wire.write(buf, n);
  return Wire.endTransmission()==0 ? n : -5;
}
int  sic_i2c_read (int bus, uint8_t addr, uint8_t* buf, int n){
  (void)bus; int got = Wire.requestFrom(int(addr), n);
  for (int i=0;i<got;i++) buf[i]=Wire.read();
  return (got==n) ? n : (got>0? got : -5);
}
int  sic_i2c_writeread(int bus, uint8_t addr, const uint8_t* wr, int nw, uint8_t* rd, int nr){
  int w = sic_i2c_write(bus, addr, wr, nw); if (w<0) return w;
  return sic_i2c_read(bus, addr, rd, nr);
}
#endif /* SIC_BACKEND_ARDUINO */
