#if defined(SGFX_HAL_ARDUINO_GENERIC) && defined(SGFX_BUS_I2C)
#include "sgfx.h"
#include "sgfx_port.h"
#include "sgfx_hal.h"
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

typedef struct {
    TwoWire* wire;
  uint8_t addr;
  int8_t pin_sda, pin_scl, pin_rst, pin_bl;
  uint32_t hz;
} i2c_bus_t;

static int i2c_begin(sgfx_bus_t* b){
    i2c_bus_t* s=(i2c_bus_t*)b->user;
  if (!s->wire) s->wire = &Wire;
#if defined(ESP32)
  s->wire->begin(s->pin_sda, s->pin_scl);
#else
  s->wire->begin();
#endif
  if (s->hz) s->wire->setClock(s->hz);
  if (s->pin_rst >=0){ pinMode(s->pin_rst, OUTPUT); digitalWrite(s->pin_rst, HIGH); }
  if (s->pin_bl  >=0){ pinMode(s->pin_bl, OUTPUT);  digitalWrite(s->pin_bl, HIGH); }
  return SGFX_OK;
}
static void i2c_end(sgfx_bus_t* b){ (void)b; }

static int i2c_write_cmd(sgfx_bus_t* b, uint8_t cmd){
    i2c_bus_t* s=(i2c_bus_t*)b->user;
  s->wire->beginTransmission(s->addr);
  s->wire->write(0x00);
  s->wire->write(cmd);
  s->wire->endTransmission();
  return SGFX_OK;
}

static int i2c_write_data(sgfx_bus_t* b, const void* buf, size_t len){
    i2c_bus_t* s=(i2c_bus_t*)b->user;
  const uint8_t* p=(const uint8_t*)buf;
  while (len){
      s->wire->beginTransmission(s->addr);
    s->wire->write(0x40);
    size_t n = len > 16 ? 16 : len;
    s->wire->write(p, n);
    s->wire->endTransmission();
    p += n; len -= n;
  }
  return SGFX_OK;
}

static int i2c_write_repeat(sgfx_bus_t* b, const void* unit, size_t unit_bytes, size_t count){
    uint8_t tmp[16];
  if (unit_bytes != 1) return SGFX_ERR_NOSUP;
  memset(tmp, *(const uint8_t*)unit, sizeof tmp);
  while (count){
      size_t n = count > sizeof(tmp) ? sizeof(tmp) : count;
    int rc = i2c_write_data(b, tmp, n);
    if (rc) return rc;
    count -= n;
  }
  return SGFX_OK;
}

static int i2c_write_pixels(sgfx_bus_t* b, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
    (void)b; (void)px; (void)count; (void)src_fmt;
  return SGFX_ERR_NOSUP;
}

static int i2c_read_data(sgfx_bus_t* b, void* buf, size_t len){ (void)b; (void)buf; (void)len; return SGFX_ERR_NOSUP; }
static void i2c_delay(sgfx_bus_t* b, uint32_t ms){ (void)b; delay(ms); }
static void i2c_gpio_set(sgfx_bus_t* b, int pin_id, bool level){
    i2c_bus_t* s=(i2c_bus_t*)b->user;
  if (pin_id==SGFX_GPIO_BL  && s->pin_bl>=0)  digitalWrite(s->pin_bl,  level?HIGH:LOW);
  if (pin_id==SGFX_GPIO_RST && s->pin_rst>=0) digitalWrite(s->pin_rst, level?HIGH:LOW);
}

static const sgfx_bus_ops_t VOPS = {
    .begin = i2c_begin, .end = i2c_end,
  .write_cmd = i2c_write_cmd, .write_data = i2c_write_data,
  .write_repeat = i2c_write_repeat, .write_pixels = i2c_write_pixels,
  .read_data = i2c_read_data, .delay_ms = i2c_delay, .gpio_set = i2c_gpio_set
};

extern "C" int sgfx_hal_make_i2c(sgfx_bus_t* out, const sgfx_hal_cfg_i2c_t* cfg){
    static i2c_bus_t state;
  memset(&state, 0, sizeof state);
  state.addr = cfg->addr ? cfg->addr : 0x3C;
  state.pin_sda = cfg->pin_sda;
  state.pin_scl = cfg->pin_scl;
  state.pin_rst = cfg->pin_rst;
  state.pin_bl  = cfg->pin_bl;
  state.hz = cfg->hz ? cfg->hz : 400000;
  out->ops = &VOPS;
  out->user = &state;
  out->hz_max = state.hz;
  out->features = 0;
  return SGFX_OK;
}
#endif