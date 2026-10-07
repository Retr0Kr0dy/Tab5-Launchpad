/*
 * console.c — Tab5-Orion console transport.
 *
 * ── Why the transport is chosen from sdkconfig, not hardcoded ──────────────
 *
 * ESP32-P4 boards in this family expose their console over the SoC's built-in
 * USB-Serial-JTAG peripheral rather than a pin-muxed UART. M5Stack's own Tab5
 * firmware confirms it for this exact SKU:
 *   M5Tab5-UserDemo/platforms/tab5/sdkconfig
 *     CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
 *     CONFIG_ESP_CONSOLE_UART_NUM=-1
 * sdkconfig is already the one place the board's real wiring is described, so
 * the driver follows it:
 *   CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG  -> driver/usb_serial_jtag.h
 *   otherwise                           -> driver/uart.h on the configured UART
 *
 * That means moving Orion's console to a UART (e.g. onto Grove Port C, GPIO7/6,
 * to keep the USB port free for host-mode experiments) is an sdkconfig change
 * plus a uart_set_pin() call, not a code restructure.
 *
 * Everything here is non-blocking by contract: konsole is polled from a single
 * loop in main.c, and every live test command uses orion_console_getch() as its
 * "any key exits" escape hatch. A blocking read would wedge both.
 */

#include "orion.h"
#include "ui_components.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || \
    defined(CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG)
#  define ORION_CONSOLE_USJ 1
#  include "driver/usb_serial_jtag.h"
#else
#  define ORION_CONSOLE_USJ 0
#  include "driver/uart.h"
#  if defined(CONFIG_ESP_CONSOLE_UART_NUM) && CONFIG_ESP_CONSOLE_UART_NUM >= 0
#    define ORION_UART_PORT ((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM)
#  else
#    define ORION_UART_PORT ((uart_port_t)0)
#  endif
#  if defined(CONFIG_ESP_CONSOLE_UART_BAUDRATE)
#    define ORION_UART_BAUD CONFIG_ESP_CONSOLE_UART_BAUDRATE
#  else
#    define ORION_UART_BAUD 115200
#  endif
/* Console UART pins: left at the ESP-IDF defaults for the selected UART
 * unless overridden at build time. */
#  ifndef ORION_UART_TX
#    define ORION_UART_TX UART_PIN_NO_CHANGE
#  endif
#  ifndef ORION_UART_RX
#    define ORION_UART_RX UART_PIN_NO_CHANGE
#  endif
#endif

/* ymodem.c's receive path sends whole 1029-byte blocks (1024-byte payload +
 * 5-byte SOH/blk/~blk/CRC framing) in a single host-side write(). A smaller
 * ring overflows before orion_console_read()'s polling loop (5ms between
 * empty reads, see xfer_read_byte() in ymodem.c) can drain it, corrupting
 * the block and forcing a NAK on every attempt. 4096 comfortably holds
 * several full blocks of slack even if the reading task is briefly
 * delayed. The write path still bursts whole banner/help blocks, so TX
 * keeps its own ring. */
#define ORION_CONSOLE_RX_BUF 4096
#define ORION_CONSOLE_TX_BUF 2048
#define ORION_CONSOLE_INJECT_BUF 256

static int64_t s_start_us;
static int     s_ready;

/* Software input queue used by the on-screen terminal and the physical Tab5
 * keyboard accessory. It feeds the exact same konsole read path as USB/UART,
 * so there is still only one command parser/line editor. */
static uint8_t s_inject[ORION_CONSOLE_INJECT_BUF];
static size_t  s_inject_r, s_inject_w, s_inject_n;

size_t orion_console_inject(const uint8_t* buf, size_t len)
{
    if (!buf || !len) return 0;
    size_t pushed = 0;
    while (pushed < len && s_inject_n < ORION_CONSOLE_INJECT_BUF) {
        s_inject[s_inject_w] = buf[pushed++];
        s_inject_w = (s_inject_w + 1) % ORION_CONSOLE_INJECT_BUF;
        ++s_inject_n;
    }
    return pushed;
}

int orion_console_init(void)
{
    s_start_us = esp_timer_get_time();
    if (s_ready) return 0;

#if ORION_CONSOLE_USJ
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = ORION_CONSOLE_TX_BUF,
        .rx_buffer_size = ORION_CONSOLE_RX_BUF,
    };
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) return -1;
#else
    uart_config_t cfg = {
        .baud_rate  = ORION_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(ORION_UART_PORT, ORION_CONSOLE_RX_BUF,
                            ORION_CONSOLE_TX_BUF, 0, NULL, 0) != ESP_OK) return -1;
    if (uart_param_config(ORION_UART_PORT, &cfg) != ESP_OK) return -1;
    if (uart_set_pin(ORION_UART_PORT, ORION_UART_TX, ORION_UART_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return -1;
#endif

    s_ready = 1;
    return 0;
}

const char* orion_console_name(void)
{
#if ORION_CONSOLE_USJ
    return "USB-Serial-JTAG";
#else
    return "UART";
#endif
}

size_t orion_console_write(const uint8_t* buf, size_t len)
{
    if (!buf || !len || !s_ready) return 0;
#if ORION_CONSOLE_USJ
    /* Bounded, not indefinite: with no host attached the TX ring fills and
     * stays full, and a blocking write would wedge the whole console loop.
     * Dropping output when nobody is listening is the right trade here. */
    int n = usb_serial_jtag_write_bytes(buf, len, pdMS_TO_TICKS(20));
#else
    int n = uart_write_bytes(ORION_UART_PORT, buf, len);
#endif
    return n < 0 ? 0u : (size_t)n;
}

size_t orion_console_read(uint8_t* buf, size_t len)
{
    if (!buf || !len) return 0;

    size_t out = 0;
    while (out < len && s_inject_n) {
        buf[out++] = s_inject[s_inject_r];
        s_inject_r = (s_inject_r + 1) % ORION_CONSOLE_INJECT_BUF;
        --s_inject_n;
    }
    if (out == len || !s_ready) return out;

    /* Zero timeout: return whatever is already buffered, never block. */
#if ORION_CONSOLE_USJ
    int n = usb_serial_jtag_read_bytes(buf + out, len - out, 0);
#else
    int n = uart_read_bytes(ORION_UART_PORT, buf + out, len - out, 0);
#endif
    if (n > 0) out += (size_t)n;
    return out;
}

int orion_console_getch(void)
{
    uint8_t c;
    /* Only used by the "any key exits" live-screen loops (touch/imu/mic/etc,
     * see this file's header) -- konsole's own prompt reads via
     * orion_console_read() directly (main.c), so filtering here never drops
     * a byte the line editor needed.
     *
     * A *fresh* serial connection (e.g. a host opening the port to run
     * `idf.py monitor` or a debug script) reliably injects line noise --
     * not just NUL bytes, arbitrary garbage -- which used to read as `>= 0`
     * and get treated as a real keypress, silently yanking the touch UI back
     * to the console prompt with nothing left to redraw it (looked like a
     * frozen splash/menu). Restrict "exit" to keys a human would actually
     * press to mean it -- Enter, Escape, Ctrl-C -- and ignore everything
     * else instead of trying to guess which raw byte values noise can take. */
    while (orion_console_read(&c, 1) == 1) {
        if (c == '\r' || c == '\n' || c == 0x1B /* ESC */ || c == 0x03 /* Ctrl-C */)
            return (int)c;
    }
    return -1;
}

void orion_console_drain(void)
{
    uint8_t sink[32];
    while (orion_console_read(sink, sizeof sink) > 0) { }
}

uint32_t orion_millis(void)
{
    return (uint32_t)((esp_timer_get_time() - s_start_us) / 1000);
}

/* ── main-task canary ──────────────────────────────────────────────────
 * Orion's whole UI -- the home menu, terminal, video, settings, and every
 * live screen (touch/IMU/mic/gamepad/etc, whether hand-rolled or on the
 * shared orion_frame_poll() abstraction) -- all run nested on ONE FreeRTOS
 * task: app_main()'s own, via the single top-level orion_touchmenu_run()
 * call chain (main.c never spawns a separate UI task). Every one of those
 * loops already calls orion_delay_ms() once per iteration -- it is the one
 * place forward progress is provable regardless of which screen is active,
 * so kicking the canary here (instead of sprinkling explicit kicks through
 * every individual loop) covers the whole UI for free.
 *
 * Guarded to the registered main-task handle specifically: video_player.c
 * and usb_hid_host.c also call orion_delay_ms() from their OWN background
 * tasks, which must not be able to mask a real stall in the main task by
 * kicking the canary on its behalf. */
static TaskHandle_t s_canary_task;
static volatile uint32_t s_canary_last_kick_ms;

void orion_canary_register_main_task(void)
{
    s_canary_task = xTaskGetCurrentTaskHandle();
    s_canary_last_kick_ms = orion_millis();
}

uint32_t orion_canary_age_ms(void)
{
    /* Unsigned subtraction wraps correctly across orion_millis()'s own
     * ~49-day rollover, same trick used throughout this file. */
    return orion_millis() - s_canary_last_kick_ms;
}

void orion_delay_ms(uint32_t ms)
{
    if (s_canary_task && xTaskGetCurrentTaskHandle() == s_canary_task) {
        s_canary_last_kick_ms = orion_millis();
    }

    /* Carousel/marquee draw-time clock, same reasoning as the canary kick
     * just above -- every screen's poll loop
     * already calls this once per iteration, so this is the one place that
     * covers all of them for free instead of threading a fresh
     * orion_millis() call through every individual redraw site. Unlike the
     * canary this isn't guarded to the main task specifically: there's no
     * "don't mask a stall" risk in also nudging it forward from
     * video_player.c's/usb_hid_host.c's background tasks, it's just a
     * shared clock. */
    orion_ui_set_time_ms(orion_millis());

    /* vTaskDelay() rounds down, so a sub-tick delay would become a bare yield
     * that never blocks — starving lower-priority work and the task watchdog.
     * Always give up at least one full tick. (CONFIG_FREERTOS_HZ=1000 in
     * sdkconfig.defaults makes one tick 1 ms.) */
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
}
