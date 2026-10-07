#if defined(SGFX_HAL_STM32CUBE)

#include "sgfx_hal.h"
#include "sgfx.h"
#include <string.h>
#include "stm32_hal.h" /* see note in stm32_hal_spi.c */

typedef struct {
  uint8_t addr;
} stm_i2c_ctx_t;

extern I2C_HandleTypeDef hi2c_sgfx;
extern void sgfx_delay_ms(uint32_t ms);

static int stm_begin(sgfx_bus_t* b){ return SGFX_OK; }
static void stm_end(sgfx_bus_t* b){ (void)b; }
static inline void stm_delay(sgfx_bus_t* b, uint32_t ms){ (void)b; sgfx_delay_ms(ms); }
static inline void stm_gpio_set(sgfx_bus_t* b, int pin_id, bool level){ (void)b; (void)pin_id; (void)level; }

static int cmd(I2C_HandleTypeDef* i2c, uint8_t addr, uint8_t c){
  uint8_t buf[2] = {0x00, c};
  return (HAL_I2C_Master_Transmit(i2c, addr<<1, buf, 2, 1000) == HAL_OK) ? SGFX_OK : -1;
}

static int data(I2C_HandleTypeDef* i2c, uint8_t addr, const void* p, size_t n){
  // Chunk to avoid large blocking writes
  const uint8_t* b = (const uint8_t*)p;
  uint8_t t[32];
  while(n){
    size_t k = (n > sizeof(t)-1) ? sizeof(t)-1 : n;
    t[0] = 0x40;
    memcpy(&t[1], b, k);
    if (HAL_I2C_Master_Transmit(i2c, addr<<1, t, k+1, 1000) != HAL_OK) return -1;
    b += k; n -= k;
  }
  return SGFX_OK;
}

static int stm_write_cmd(sgfx_bus_t* b, uint8_t cmd){
  stm_i2c_ctx_t* c = (stm_i2c_ctx_t*)b->user;
  return cmd(&hi2c_sgfx, c->addr, cmd);
}

static int stm_write_data(sgfx_bus_t* b, const void* buf, size_t len){
  stm_i2c_ctx_t* c = (stm_i2c_ctx_t*)b->user;
  return data(&hi2c_sgfx, c->addr, buf, len);
}

static int stm_write_repeat(sgfx_bus_t* b, const void* unit, size_t unit_bytes, size_t count){
  for (size_t i=0;i<count;i++){ int rc = stm_write_data(b, unit, unit_bytes); if (rc) return rc; }
  return SGFX_OK;
}

static int stm_write_pixels(sgfx_bus_t* b, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
  (void)b;(void)px;(void)count;(void)src_fmt; return -1;
}

static int stm_read_data(sgfx_bus_t* b, void* buf, size_t len){ (void)b;(void)buf;(void)len; return -1; }

static const sgfx_bus_ops_t STM_I2C_OPS = {
  .begin=stm_begin,.end=stm_end,.write_cmd=stm_write_cmd,.write_data=stm_write_data,
  .write_repeat=stm_write_repeat,.write_pixels=stm_write_pixels,.read_data=stm_read_data,
  .delay_ms=stm_delay,.gpio_set=stm_gpio_set
};

int sgfx_hal_make_i2c(sgfx_bus_t* out, const sgfx_hal_cfg_i2c_t* cfg){
  if (!out || !cfg) return -1;
  stm_i2c_ctx_t* c = (stm_i2c_ctx_t*)calloc(1, sizeof(*c));
  if (!c) return -1;
  c->addr = cfg->addr;
  out->ops = &STM_I2C_OPS;
  out->user = c;
  out->hz_max = cfg->hz;
  out->features = 0;
  return SGFX_OK;
}

#endif /* SGFX_HAL_STM32CUBE */