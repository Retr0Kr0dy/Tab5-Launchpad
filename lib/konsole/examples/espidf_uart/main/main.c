\
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "konsole/konsole.h"
#include "konsole/konsole_io.h"

#define UART_NUM UART_NUM_0

static int uart_write(const uint8_t* d, size_t n) {
    return uart_write_bytes(UART_NUM, (const char*)d, n);
}

static int uart_read(uint8_t* d, size_t n) {
    int r = uart_read_bytes(UART_NUM, d, n, 10 / portTICK_PERIOD_MS);
    return r < 0 ? 0 : r;
}

void app_main(void) {
    const uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_REF_TICK
    };
    uart_driver_install(UART_NUM, 2048, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &cfg);
    uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    konsole_io io = { uart_read, uart_write, NULL };
    konsole_init(&io);
    kon_printf("konsole ready\\r\\n");

    while (1) {
        konsole_poll();
        vTaskDelay(1);
    }
}
