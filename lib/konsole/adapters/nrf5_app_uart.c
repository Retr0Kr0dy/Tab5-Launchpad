#if defined(KONSOLE_ADAPTER_NRF5) || defined(NRF51) || defined(NRF52)

#include <string.h>
#include <stdint.h>
#include "../konsole.h"
#include "../io.h"

#ifndef KONSOLE_NRF5_RX_BUF
#define KONSOLE_NRF5_RX_BUF 256
#endif
typedef struct {
  volatile uint16_t head, tail;
  uint8_t buf[KONSOLE_NRF5_RX_BUF];
}
konsole_nrf5_ctx_t;
static inline size_t rb_count(konsole_nrf5_ctx_t * c) {
  return (uint16_t)(c -> head - c -> tail);
}
static inline void rb_push(konsole_nrf5_ctx_t * c, uint8_t b) {
  uint16_t next = (uint16_t)(c -> head + 1);
  c -> buf[c -> head % KONSOLE_NRF5_RX_BUF] = b;
  c -> head = next;
  if ((uint16_t)(c -> head - c -> tail) > KONSOLE_NRF5_RX_BUF) c -> tail++;
}
static inline size_t rb_popn(konsole_nrf5_ctx_t * c, uint8_t * out, size_t n) {
  size_t i = 0;
  while (i < n && rb_count(c)) {
    out[i++] = c -> buf[c -> tail % KONSOLE_NRF5_RX_BUF];
    c -> tail++;
  }
  return i;
}
static size_t ks_read_avail(void * ctx) {
  return rb_count((konsole_nrf5_ctx_t * ) ctx);
}
static size_t ks_read(void * ctx, uint8_t * buf, size_t len) {
  return rb_popn((konsole_nrf5_ctx_t * ) ctx, buf, len);
}
extern int app_uart_put(uint8_t c);
static size_t ks_write(void * ctx,
  const uint8_t * buf, size_t len) {
  (void) ctx;
  size_t i = 0;
  while (i < len) {
    if (app_uart_put(buf[i]) == 0) i++;
  }
  return i;
}
static uint32_t ks_millis(void * ) {
  return 0;
}
void konsole_nrf5_app_uart_init_ctx(konsole_nrf5_ctx_t * ctx) {
  memset(ctx, 0, sizeof( * ctx));
}
konsole_io konsole_nrf5_app_uart_io(konsole_nrf5_ctx_t * ctx) {
  return (konsole_io) {
    ks_read_avail,
    ks_read,
    ks_write,
    ks_millis,
    ctx
  };
}
void konsole_nrf5_app_uart_on_rx(konsole_nrf5_ctx_t * ctx, uint8_t byte) {
  rb_push(ctx, byte);
}
#endif