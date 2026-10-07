#ifdef SGFX_HAL_ESPIDF

#include "sgfx_hal.h"
#include "sgfx.h"
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef SGFX_IDF_SPI_HOST
  /* Prefer SPI2_HOST on S2/S3/C3/C6; fall back to HSPI_HOST on classic ESP32 */
  #ifdef SPI2_HOST
    #define SGFX_IDF_SPI_HOST SPI2_HOST
  #elif defined(HSPI_HOST)
    #define SGFX_IDF_SPI_HOST HSPI_HOST
  #else
    #define SGFX_IDF_SPI_HOST SPI2_HOST
  #endif
#endif

typedef struct {
  spi_device_handle_t dev;
  int pin_dc, pin_rst, pin_bl;
  uint32_t hz;
} idf_spi_ctx_t;

static int idf_begin(sgfx_bus_t* b){ return SGFX_OK; }
static void idf_end(sgfx_bus_t* b){ (void)b; }

static inline void idf_delay(sgfx_bus_t* b, uint32_t ms){
  (void)b;
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline void idf_gpio_set(sgfx_bus_t* b, int pin_id, bool level){
  idf_spi_ctx_t* c = (idf_spi_ctx_t*)b->user;
  int pin = -1;
  switch(pin_id){
    case SGFX_GPIO_DC:  pin = c->pin_dc; break;
    case SGFX_GPIO_BL:  pin = c->pin_bl; break;
    case SGFX_GPIO_RST: pin = c->pin_rst; break;
    default: break;
  }
  if (pin >= 0){
    gpio_set_level(pin, level ? 1 : 0);
  }
}

static int idf_tx_cmddata(spi_device_handle_t dev, const void* data, size_t len, int dc_level){
  spi_transaction_t t = {0};
  t.flags = 0;
  t.length = len * 8;
  t.tx_buffer = data;
  // DC pin handled out of band via gpio_set
  esp_err_t err = spi_device_transmit(dev, &t);
  return (err==ESP_OK) ? SGFX_OK : -1;
}

static int idf_write_cmd(sgfx_bus_t* b, uint8_t cmd){
  idf_spi_ctx_t* c = (idf_spi_ctx_t*)b->user;
  if (c->pin_dc >= 0) gpio_set_level(c->pin_dc, 0);
  return idf_tx_cmddata(c->dev, &cmd, 1, 0);
}

static int idf_write_data(sgfx_bus_t* b, const void* buf, size_t len){
  idf_spi_ctx_t* c = (idf_spi_ctx_t*)b->user;
  if (c->pin_dc >= 0) gpio_set_level(c->pin_dc, 1);
  return idf_tx_cmddata(c->dev, buf, len, 1);
}

static int idf_write_repeat(sgfx_bus_t* b, const void* unit, size_t unit_bytes, size_t count){
  // naive loop; could be optimized with a small cache buffer
  for (size_t i=0;i<count;i++){
    int rc = idf_write_data(b, unit, unit_bytes);
    if (rc) return rc;
  }
  return SGFX_OK;
}

static int idf_write_pixels(sgfx_bus_t* b, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
  // For ST7789 we typically push RGB565
  if (src_fmt != SGFX_FMT_RGB565){
    // Fallback: treat as raw bytes
    return idf_write_data(b, px, count);
  }
  return idf_write_data(b, px, count*2);
}

static int idf_read_data(sgfx_bus_t* b, void* buf, size_t len){
  (void)b; (void)buf; (void)len;
  return -1; // not implemented
}

static const sgfx_bus_ops_t IDF_SPI_OPS = {
  .begin = idf_begin,
  .end = idf_end,
  .write_cmd = idf_write_cmd,
  .write_data = idf_write_data,
  .write_repeat = idf_write_repeat,
  .write_pixels = idf_write_pixels,
  .read_data = idf_read_data,
  .delay_ms = idf_delay,
  .gpio_set = idf_gpio_set
};

int sgfx_hal_make_spi(sgfx_bus_t* out, const sgfx_hal_cfg_spi_t* cfg){
  if (!out || !cfg) return -1;

  // Init GPIOs for DC/RST/BL
  if (cfg->pin_dc >= 0){ gpio_config_t io = { .pin_bit_mask = 1ULL<<cfg->pin_dc, .mode=GPIO_MODE_OUTPUT }; gpio_config(&io); }
  if (cfg->pin_rst >= 0){ gpio_config_t io = { .pin_bit_mask = 1ULL<<cfg->pin_rst, .mode=GPIO_MODE_OUTPUT }; gpio_config(&io); }
  if (cfg->pin_bl >= 0){ gpio_config_t io = { .pin_bit_mask = 1ULL<<cfg->pin_bl, .mode=GPIO_MODE_OUTPUT }; gpio_config(&io); }

  // SPI bus + device
  spi_bus_config_t buscfg = {
    .mosi_io_num = cfg->pin_mosi,
    .miso_io_num = (cfg->pin_miso>=0)?cfg->pin_miso:-1,
    .sclk_io_num = cfg->pin_sck,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 4096
  };
  if (spi_bus_initialize(SGFX_IDF_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) return -1;

  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = (int)cfg->hz,
    .mode = 0,
    .spics_io_num = cfg->pin_cs,
    .queue_size = 1,
  };
  spi_device_handle_t dev;
  if (spi_bus_add_device(SGFX_IDF_SPI_HOST, &devcfg, &dev) != ESP_OK) return -1;

  idf_spi_ctx_t* c = (idf_spi_ctx_t*)calloc(1, sizeof(*c));
  if (!c) return -1;
  c->dev = dev; c->pin_dc = cfg->pin_dc; c->pin_rst = cfg->pin_rst; c->pin_bl = cfg->pin_bl; c->hz = cfg->hz;

  out->ops = &IDF_SPI_OPS;
  out->user = c;
  out->hz_max = cfg->hz;
  out->features = 0;
  return SGFX_OK;
}

#endif /* SGFX_HAL_ESPIDF */