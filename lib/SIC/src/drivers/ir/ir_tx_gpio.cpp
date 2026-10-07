#if defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)
#define SIC_AUDIO_NO_COMPAT 1
#include <Arduino.h>
#include <string.h>
extern "C" {
  #include "sic/sic.h"
  #include "sic/ir/ir_tx.h"
  #include "sic/sic_registry.h"
}

typedef struct {
    sic_ir_tx_cfg_t cfg;
    uint8_t off_level;
    uint8_t on_level;
} ir_gpio_ctx_t;

static ir_gpio_ctx_t g_ctx;
static ir_t g_ir;

static inline uint32_t carrier_hz(const ir_gpio_ctx_t* c) {
    return (c && c->cfg.carrier_hz) ? c->cfg.carrier_hz : 38000u;
}

static void ir_space(const ir_gpio_ctx_t* c, uint32_t usec) {
    digitalWrite(c->cfg.pin, c->off_level);
    delayMicroseconds(usec);
}

static void ir_mark(const ir_gpio_ctx_t* c, uint32_t usec) {
    const uint32_t hz = carrier_hz(c);
    uint32_t half = 1000000u / (hz * 2u);
    if (half < 3u) half = 3u;
    uint32_t start = micros();
    while ((uint32_t)(micros() - start) < usec) {
        digitalWrite(c->cfg.pin, c->on_level);
        delayMicroseconds(half);
        digitalWrite(c->cfg.pin, c->off_level);
        delayMicroseconds(half);
    }
}

static int ir_send_nec(const void* self, uint32_t code) {
    const ir_gpio_ctx_t* c = (const ir_gpio_ctx_t*)((const ir_t*)self)->impl;
    if (!c || c->cfg.pin < 0) return -1;

    ir_mark(c, 9000);
    ir_space(c, 4500);
    for (unsigned i = 0; i < 32; ++i) {
        ir_mark(c, 560);
        ir_space(c, (code & (1UL << i)) ? 1690 : 560);
    }
    ir_mark(c, 560);
    ir_space(c, 0);
    return 0;
}

static const struct ir_vtbl_s IR_GPIO_VT = { ir_send_nec };

static int probe_ir_gpio(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "ir_gpio") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_ir_tx_cfg_t*)d->cfg;
    g_ctx.on_level = g_ctx.cfg.active_high ? HIGH : LOW;
    g_ctx.off_level = g_ctx.cfg.active_high ? LOW : HIGH;
    if (g_ctx.cfg.pin >= 0) {
        pinMode(g_ctx.cfg.pin, OUTPUT);
        digitalWrite(g_ctx.cfg.pin, g_ctx.off_level);
    }
    g_ir.v = &IR_GPIO_VT;
    g_ir.impl = &g_ctx;
    *out = &g_ir;
    return 0;
}

static const sic_driver_t DRV_IR_GPIO = { "ir_gpio", SIC_F_IR_TX, probe_ir_gpio, NULL };

extern "C" void sic_register_driver_ir_gpio(void) {
    sic_registry_register(&DRV_IR_GPIO);
}
#endif
