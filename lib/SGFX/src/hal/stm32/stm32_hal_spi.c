#if defined(SGFX_HAL_STM32CUBE)

#include "sgfx_hal.h"
#include "sgfx.h"
#include <string.h>
#include "stm32_hal.h" /* see note below */

/* ===== NOTE =====
 * Provide a thin "stm32_hal.h" in your project that includes the proper HAL header
 * for your MCU (e.g., stm32f1xx_hal.h / stm32g0xx_hal.h) and declares:
 *   extern SPI_HandleTypeDef hspi_sgfx;
 *   void sgfx_delay_ms(uint32_t ms);
 *   void sgfx_gpio_set_dc(int level);
 *   void sgfx_gpio_set_cs(int level);
 *   void sgfx_gpio_set_rst(int level);
 *   void sgfx_gpio_set_bl(int level);
 */

typedef struct {
  uint32_t hz;
} stm_spi_ctx_t;

extern SPI_HandleTypeDef hspi_sgfx;
extern void sgfx_delay_ms(uint32_t ms);
extern void sgfx_gpio_set_dc(int level);
extern void sgfx_gpio_set_cs(int level);
extern void sgfx_gpio_set_rst(int level);
extern void sgfx_gpio_set_bl(int level);

static int stm_begin(sgfx_bus_t* b){ sgfx_gpio_set_cs(0); return SGFX_OK; }
static void stm_end(sgfx_bus_t* b){ sgfx_gpio_set_cs(1); }

static inline void stm_delay(sgfx_bus_t* b, uint32_t ms){ (void)b; sgfx_delay_ms(ms); }

static inline void stm_gpio_set(sgfx_bus_t* b, int pin_id, bool level){
  (void)b;
  switch(pin_id){
    case SGFX_GPIO_DC:  sgfx_gpio_set_dc(level);  break;
    case SGFX_GPIO_BL:  sgfx_gpio_set_bl(level);  break;
    case SGFX_GPIO_RST: sgfx_gpio_set_rst(level); break;
    default: break;
  }
}

static int tx(const void* data, size_t len){
  return (HAL_SPI_Transmit(&hspi_sgfx, (uint8_t*)data, (uint16_t)len, 1000) == HAL_OK) ? SGFX_OK : -1;
}

static int stm_write_cmd(sgfx_bus_t* b, uint8_t cmd){
  (void)b;
  sgfx_gpio_set_dc(0);
  return tx(&cmd, 1);
}

static int stm_write_data(sgfx_bus_t* b, const void* buf, size_t len){
  (void)b;
  sgfx_gpio_set_dc(1);
  return tx(buf, len);
}

static int stm_write_repeat(sgfx_bus_t* b, const void* unit, size_t unit_bytes, size_t count){
  for (size_t i=0;i<count;i++){ int rc = stm_write_data(b, unit, unit_bytes); if (rc) return rc; }
  return SGFX_OK;
}

static int stm_write_pixels(sgfx_bus_t* b, const void* px, size_t count, sgfx_pixfmt_t src_fmt){
  if (src_fmt == SGFX_FMT_RGB565) return stm_write_data(b, px, count*2);
  return stm_write_data(b, px, count);
}

static int stm_read_data(sgfx_bus_t* b, void* buf, size_t len){
  (void)b; (void)buf; (void)len; return -1;
}

static const sgfx_bus_ops_t STM_SPI_OPS = {
  .begin = stm_begin, .end = stm_end,
  .write_cmd = stm_write_cmd, .write_data = stm_write_data,
  .write_repeat = stm_write_repeat, .write_pixels = stm_write_pixels,
  .read_data = stm_read_data, .delay_ms = stm_delay, .gpio_set = stm_gpio_set
};

int sgfx_hal_make_spi(sgfx_bus_t* out, const sgfx_hal_cfg_spi_t* cfg){
  if (!out || !cfg) return -1;
  (void)cfg; // clocks and pins are expected to be configured in MX code / user init
  out->ops = &STM_SPI_OPS;
  out->user = NULL;
  out->hz_max = cfg->hz;
  out->features = 0;
  // Ensure CS is deasserted when idle (active low)
  sgfx_gpio_set_cs(1);
  return SGFX_OK;
}

#endif /* SGFX_HAL_STM32CUBE */