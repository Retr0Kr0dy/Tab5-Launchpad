#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include <string.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "../konsole.h"
#include "../io.h"

typedef struct {
  uart_port_t port;
}
konsole_espidf_ctx_t;
static size_t ks_read_avail(void * ctx) {
  konsole_espidf_ctx_t * c = (konsole_espidf_ctx_t * ) ctx;
  size_t len = 0;
  uart_get_buffered_data_len(c -> port, & len);
  return len;
}
static size_t ks_read(void * ctx, uint8_t * buf, size_t len) {
  konsole_espidf_ctx_t * c = (konsole_espidf_ctx_t * ) ctx;
  int n = uart_read_bytes(c -> port, buf, len, 0);
  return (n > 0) ? (size_t) n : 0;
}
static size_t ks_write(void * ctx,
  const uint8_t * buf, size_t len) {
  konsole_espidf_ctx_t * c = (konsole_espidf_ctx_t * ) ctx;
  int n = uart_write_bytes(c -> port, (const char * ) buf, len);
  return (n > 0) ? (size_t) n : 0;
}
static uint32_t ks_millis(void * ) {
  return (uint32_t)(esp_timer_get_time() / 1000 ULL);
}
konsole_io konsole_espidf_uart_io(uart_port_t port) {
  static konsole_espidf_ctx_t ctx;
  ctx.port = port;
  return (konsole_io) {
    ks_read_avail,
    ks_read,
    ks_write,
    ks_millis,
    & ctx
  };
}
#endif