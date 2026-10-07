#if defined(KONSOLE_ADAPTER_STM32) || \
  defined(STM32F0xx) || defined(STM32F1xx) || defined(STM32F2xx) || defined(STM32F3xx) || \
  defined(STM32F4xx) || defined(STM32F7xx) || defined(STM32G0xx) || defined(STM32G4xx) || \
  defined(STM32H7xx) || defined(STM32L0xx) || defined(STM32L1xx) || defined(STM32L4xx) || \
  defined(STM32L5xx) || defined(STM32U5xx) || defined(STM32WBxx) || defined(STM32WLxx)
  
#include <string.h>
#include <stdint.h>
#include "../konsole.h"
#include "../io.h"

typedef struct __UART_HandleTypeDef UART_HandleTypeDef;
extern int HAL_UART_Transmit(UART_HandleTypeDef * huart, uint8_t * pData, uint16_t Size, uint32_t Timeout);
extern uint32_t HAL_GetTick(void);
#ifndef KONSOLE_STM32_RX_BUF
#define KONSOLE_STM32_RX_BUF 256
#endif
typedef struct {
  UART_HandleTypeDef * huart;
  volatile uint16_t head, tail;
  uint8_t buf[KONSOLE_STM32_RX_BUF];
  uint8_t rx_byte;
}
konsole_stm32_ctx_t;
static inline size_t rb_count(konsole_stm32_ctx_t * c) {
  return (uint16_t)(c -> head - c -> tail);
}
static inline void rb_push(konsole_stm32_ctx_t * c, uint8_t b) {
  uint16_t next = (uint16_t)(c -> head + 1);
  c -> buf[c -> head % KONSOLE_STM32_RX_BUF] = b;
  c -> head = next;
  if ((uint16_t)(c -> head - c -> tail) > KONSOLE_STM32_RX_BUF) c -> tail++;
}
static inline size_t rb_popn(konsole_stm32_ctx_t * c, uint8_t * out, size_t n) {
  size_t i = 0;
  while (i < n && rb_count(c)) {
    out[i++] = c -> buf[c -> tail % KONSOLE_STM32_RX_BUF];
    c -> tail++;
  }
  return i;
}
static size_t ks_read_avail(void * ctx) {
  return rb_count((konsole_stm32_ctx_t * ) ctx);
}
static size_t ks_read(void * ctx, uint8_t * buf, size_t len) {
  return rb_popn((konsole_stm32_ctx_t * ) ctx, buf, len);
}
static size_t ks_write(void * ctx,
  const uint8_t * buf, size_t len) {
  konsole_stm32_ctx_t * c = (konsole_stm32_ctx_t * ) ctx;
  if (!c -> huart) return 0;
  return (HAL_UART_Transmit(c -> huart, (uint8_t * ) buf, (uint16_t) len, 1000) == 0) ? len : 0;
}
static uint32_t ks_millis(void * ) {
  return HAL_GetTick();
}
void konsole_stm32_hal_uart_init_ctx(konsole_stm32_ctx_t * ctx, UART_HandleTypeDef * huart) {
  memset(ctx, 0, sizeof( * ctx));
  ctx -> huart = huart;
}
konsole_io konsole_stm32_hal_uart_io(konsole_stm32_ctx_t * ctx) {
  return (konsole_io) {
    ks_read_avail,
    ks_read,
    ks_write,
    ks_millis,
    ctx
  };
}
void konsole_stm32_hal_uart_on_rx(konsole_stm32_ctx_t * ctx) {
  rb_push(ctx, ctx -> rx_byte);
}
#endif