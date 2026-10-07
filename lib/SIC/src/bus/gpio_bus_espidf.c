/*
 * gpio_bus_espidf.c — ESP-IDF native GPIO backend.
 *
 * Implements the GPIO half of the backend contract documented in
 * include/sic/sic_backend.h. Board-agnostic: no pin numbers, no board
 * assumptions — any ESP-IDF target may use this file.
 *
 * SIC_NOPIN (0xFF) is a deliberate "this board does not wire that signal"
 * marker, so every entry point silently no-ops on it (sic_gpio_read() reports
 * 1 / idle-high, matching the Arduino backend's behaviour).
 */
#if defined(SIC_BACKEND_ESPIDF)

#include <stdint.h>

#include "driver/gpio.h"

#include "sic/bus/gpio_bus.h"

/*
 * gpio_config() rather than a bare gpio_set_direction(): gpio_set_direction()
 * only flips the input/output enables, it does not route the pad through the
 * GPIO matrix. gpio_config() does both (and the pull-resistor setup) in one
 * call, which is what Arduino's pinMode() effectively guarantees and what SIC
 * drivers assume when they take a pin straight from a board descriptor.
 */
static void sic_gpio_apply(int pin, gpio_mode_t mode, int pullup, int pulldown)
{
    /* Zero-init first: gpio_config_t grows target-conditional members (e.g.
     * hys_ctrl_mode on SoCs with SOC_GPIO_SUPPORT_PIN_HYS_FILTER), and this
     * file must stay correct on every ESP-IDF target. */
    gpio_config_t cfg = {0};
    cfg.pin_bit_mask = (1ULL << (uint32_t)pin);
    cfg.mode         = mode;
    cfg.pull_up_en   = pullup   ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    (void)gpio_config(&cfg);
}

/* Valid GPIO indices are 0..GPIO_NUM_MAX-1; SIC_NOPIN (0xFF) and negatives
 * are rejected here so a mis-populated board descriptor cannot fault. */
static int sic_gpio_ok(int pin)
{
    return (pin >= 0) && (pin != (int)SIC_NOPIN) && (pin < GPIO_NUM_MAX);
}

void sic_gpio_mode(int pin, int output)
{
    if (!sic_gpio_ok(pin)) return;
    sic_gpio_apply(pin, output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT, 0, 0);
}

void sic_gpio_write(int pin, int val)
{
    if (!sic_gpio_ok(pin)) return;
    (void)gpio_set_level((gpio_num_t)pin, val ? 1u : 0u);
}

int sic_gpio_read(int pin)
{
    if (!sic_gpio_ok(pin)) return 1; /* unwired signal reads idle-high */
    return gpio_get_level((gpio_num_t)pin);
}

void sic_gpio_mode_pullup(int pin)
{
    if (!sic_gpio_ok(pin)) return;
    sic_gpio_apply(pin, GPIO_MODE_INPUT, 1, 0);
}

void sic_gpio_mode_pulldown(int pin)
{
    if (!sic_gpio_ok(pin)) return;
    sic_gpio_apply(pin, GPIO_MODE_INPUT, 0, 1);
}

#endif /* SIC_BACKEND_ESPIDF */
