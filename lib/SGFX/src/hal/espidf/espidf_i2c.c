#if defined(SGFX_HAL_ESPIDF)

#include "sgfx_hal.h"
#include "sgfx.h"
#include <string.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef SGFX_IDF_I2C_PORT
  #ifdef I2C_NUM_0
    #define SGFX_IDF_I2C_PORT I2C_NUM_0
  #else
    #define SGFX_IDF_I2C_PORT 0
  #endif
#endif

typedef struct {
  uint8_t addr;
} idf_i2c_ctx_t;

static int idf_begin(sgfx_bus_t* b){ return SGFX_OK; }
static void idf_end(sgfx_bus_t* b){ (void)b; }

static inline void idf_delay(sgfx_bus_t* b, uint32_t ms){
  (void)b;
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline void idf_gpio_set(sgfx_bus_t* b, int pin_id, bool level){
  (void)b; (void)pin_id; (void)level;
  // For SSD1306 we don't typically need DC; RST/BL can be wired, else add pins to cfg+init if desired.
}

static int idf_write_cmd(sgfx_bus_t* b, uint8_t cmd){
  idf_i2c_ctx_t* c = (idf_i2c_ctx_t*)b->user;
  uint8_t buf[2] = {0x00, cmd}; // control byte = 0x00 for commands
  esp_err_t err = i2c_master_write_to_device(SGFX_IDF_I2C_PORT, c->addr, buf, 2, 100/portTICK_PERIOD_MS);
  return (err==ESP_OK)?SGFX_OK:-1;
}

static int idf_write_data(sgfx_bus_t* b, const void* buf, size_t len){
  idf_i2c_ctx_t* c = (idf_i2c_ctx_t*)b->user;
  // Prepend 0x40 control byte for data
  // We'll send in chunks
  const uint8_t* p = (const uint8_t*)buf;
  uint8_t temp[32];
  while(len){
    size_t n = len;
    if (n > sizeof(temp)-1) n = sizeof(temp)-1;
    temp[0] = 0x40;
    memcpy(&temp[1], p, n);
    esp_err_t err = i2c_master_write_to_device(SGFX_IDF_I2C_PORT, c->addr, temp, n+1, 100/portTICK_PERIOD_MS);
    if (err != ESP_OK) return -1;
    p += n; len -= n;
  }
  return SGFX_OK;
}

static int idf_write_repeat(sgfx_bus_t* b, const void* unit, size_t unit_bytes, size_t count){
  // simple loop
  for (size_t i=0;i<count;i++){
    int rc = idf_write_data(b, unit, unit_bytes);
    if (rc) return rc;
  }
  return SGFX_OK;
}

static int idf_write_pixels(sgfx_bus_t* b, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
  // SSD1306 path sends raw page data via write_data; not used here
  (void)b; (void)px; (void)count; (void)src_fmt;
  return -1;
}

static int idf_read_data(sgfx_bus_t* b, void* buf, size_t len){
  (void)b; (void)buf; (void)len;
  return -1;
}

static const sgfx_bus_ops_t IDF_I2C_OPS = {
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

int sgfx_hal_make_i2c(sgfx_bus_t* out, const sgfx_hal_cfg_i2c_t* cfg){
  if (!out || !cfg) return -1;

  i2c_config_t conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = cfg->pin_sda,
    .scl_io_num = cfg->pin_scl,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = cfg->hz
  };
  if (i2c_param_config(SGFX_IDF_I2C_PORT, &conf) != ESP_OK) return -1;
  if (i2c_driver_install(SGFX_IDF_I2C_PORT, conf.mode, 0, 0, 0) != ESP_OK) return -1;

  idf_i2c_ctx_t* c = (idf_i2c_ctx_t*)calloc(1, sizeof(*c));
  if (!c) return -1;
  c->addr = cfg->addr;

  out->ops = &IDF_I2C_OPS;
  out->user = c;
  out->hz_max = cfg->hz;
  out->features = 0;
  return SGFX_OK;
}

#endif /* SGFX_HAL_ESPIDF */