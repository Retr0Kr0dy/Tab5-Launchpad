#ifndef SGFX_SPI_CHUNK_BYTES
#define SGFX_SPI_CHUNK_BYTES 4096
#endif
#if defined(SGFX_HAL_ARDUINO_GENERIC) && defined(SGFX_BUS_SPI)
#include "sgfx.h"
#include "sgfx_port.h"
#include "sgfx_hal.h"
#include <Arduino.h>
#include <SPI.h>
#include <string.h>

typedef struct {
    SPIClass* spi;
  int8_t pin_sck, pin_mosi, pin_miso;
  int8_t pin_cs, pin_dc, pin_rst, pin_bl;
  uint32_t hz;
  SPISettings settings;
} spi_bus_t;


static int spi_begin(sgfx_bus_t* b){
  spi_bus_t* s = (spi_bus_t*)b->user;

  // Use the global SPI object on all ESP32 variants (S3/C3/C6 + classic)
  s->spi = &SPI;

  // Map pins and initialize
  int sck  = (s->pin_sck  >= 0) ? s->pin_sck  : SCK;
  int mosi = (s->pin_mosi >= 0) ? s->pin_mosi : MOSI;
  int miso = (s->pin_miso >= 0) ? s->pin_miso : MISO;
  int cs   = (s->pin_cs   >= 0) ? s->pin_cs   : SS;

  // Ensure control GPIOs are outputs
  if (s->pin_cs  >= 0) pinMode(s->pin_cs,  OUTPUT), digitalWrite(s->pin_cs,  HIGH);
  if (s->pin_dc  >= 0) pinMode(s->pin_dc,  OUTPUT), digitalWrite(s->pin_dc,  HIGH);
  if (s->pin_rst >= 0) pinMode(s->pin_rst, OUTPUT), digitalWrite(s->pin_rst, HIGH);
  if (s->pin_bl  >= 0) pinMode(s->pin_bl,  OUTPUT), digitalWrite(s->pin_bl,  HIGH);

  // Start SPI on the requested pins (MISO may be -1 for write-only panels)
  s->spi->begin(sck, (s->pin_miso >= 0 ? miso : -1), mosi, (s->pin_cs >= 0 ? cs : -1));

  // SPI clock/format
  s->settings = SPISettings(s->hz ? s->hz : 40000000, MSBFIRST, SPI_MODE0);
  return SGFX_OK;
}

static void spi_end(sgfx_bus_t* b){
    (void)b; /* keep bus alive */
}

static inline void cs_low(const spi_bus_t* s){ if (s->pin_cs >= 0) digitalWrite(s->pin_cs, LOW); }
static inline void cs_high(const spi_bus_t* s){ if (s->pin_cs >= 0) digitalWrite(s->pin_cs, HIGH); }

static int spi_write_cmd(sgfx_bus_t* b, uint8_t cmd){
    spi_bus_t* s = (spi_bus_t*)b->user;
  s->spi->beginTransaction(s->settings);
  cs_low(s);
  if (s->pin_dc >= 0) digitalWrite(s->pin_dc, LOW);
#if defined(ESP32)
  s->spi->writeBytes(&cmd, 1);
#else
  s->spi->transfer(cmd);
#endif
  if (s->pin_dc >= 0) digitalWrite(s->pin_dc, HIGH);
  cs_high(s);
  s->spi->endTransaction();
  return SGFX_OK;
}

static int spi_write_data(sgfx_bus_t* b, const void* buf, size_t len){
    spi_bus_t* s = (spi_bus_t*)b->user;
  s->spi->beginTransaction(s->settings);
  cs_low(s);
#if defined(ESP32)
  s->spi->writeBytes((const uint8_t*)buf, len);
#else
  const uint8_t* p=(const uint8_t*)buf;
  while(len--) s->spi->transfer(*p++);
#endif
  cs_high(s);
  s->spi->endTransaction();
  return SGFX_OK;
}

static int spi_write_repeat(sgfx_bus_t* b, const void* unit, size_t unit_bytes, size_t count){
    spi_bus_t* s = (spi_bus_t*)b->user;
  if (unit_bytes != 2) return SGFX_ERR_NOSUP; /* RGB565 */
  const uint16_t v = *(const uint16_t*)unit;
  static uint16_t cache[256];
  for (size_t i=0;i<sizeof(cache)/sizeof(cache[0]);++i) cache[i]=v;

  s->spi->beginTransaction(s->settings);
  cs_low(s);
  while (count){
      size_t n = count > (sizeof(cache)/sizeof(cache[0])) ? (sizeof(cache)/sizeof(cache[0])) : count;
    s->spi->writeBytes((const uint8_t*)cache, n*2);
    count -= n;
  }
  cs_high(s);
  s->spi->endTransaction();
  return SGFX_OK;
}

static int spi_write_pixels(sgfx_bus_t* b, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
    if (src_fmt != SGFX_FMT_RGB565) return SGFX_ERR_NOSUP;
  return spi_write_data(b, px, count*2);
}

static int spi_read_data(sgfx_bus_t* b, void* buf, size_t len){
  #if defined(ESP32)
  spi_bus_t* s = (spi_bus_t*)b->user;
  s->spi->beginTransaction(s->settings);
  cs_low(s);
  s->spi->transferBytes(NULL, (uint8_t*)buf, len);
  cs_high(s);
  s->spi->endTransaction();
  return SGFX_OK;
#else
  (void)b; (void)buf; (void)len; return SGFX_ERR_NOSUP;
#endif
}

static void spi_delay(sgfx_bus_t* b, uint32_t ms){ (void)b; delay(ms); }

static void spi_gpio_set(sgfx_bus_t* b, int pin_id, bool level){
    spi_bus_t* s = (spi_bus_t*)b->user;
  int pin = -1;
  switch (pin_id){
    case SGFX_GPIO_DC:  pin = s->pin_dc;  break;
    case SGFX_GPIO_BL:  pin = s->pin_bl;  break;
    case SGFX_GPIO_RST: pin = s->pin_rst; break;
    default: break;
  }
  /* CS is intentionally not exposed through gpio_set(); SPI write paths own CS. */
  if (pin >= 0) digitalWrite(pin, level ? HIGH : LOW);
}

static const sgfx_bus_ops_t VOPS = {
    .begin = spi_begin, .end = spi_end,
  .write_cmd = spi_write_cmd, .write_data = spi_write_data,
  .write_repeat = spi_write_repeat, .write_pixels = spi_write_pixels,
  .read_data = spi_read_data, .delay_ms = spi_delay, .gpio_set = spi_gpio_set
};

extern "C" int sgfx_hal_make_spi(sgfx_bus_t* out, const sgfx_hal_cfg_spi_t* cfg){
    static spi_bus_t state;
  memset(&state, 0, sizeof state);
  state.pin_sck = cfg->pin_sck;
  state.pin_mosi = cfg->pin_mosi;
  state.pin_miso = cfg->pin_miso;
  state.pin_cs = cfg->pin_cs;
  state.pin_dc = cfg->pin_dc;
  state.pin_rst = cfg->pin_rst;
  state.pin_bl = cfg->pin_bl;
  state.hz = cfg->hz ? cfg->hz : 40000000;
  out->ops = &VOPS;
  out->user = &state;
  out->hz_max = state.hz;
  out->features = 0;
  return SGFX_OK;
}
#endif



