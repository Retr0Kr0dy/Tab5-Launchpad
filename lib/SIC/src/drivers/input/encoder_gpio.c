/*
 * encoder_gpio.c — polled quadrature rotary encoder driver for SIC.
 *
 * Decodes a standard EC11-style 2-phase (A+B) encoder using software polling.
 * One detent = one unit of delta in most common encoders.
 *
 * Build-flag hint: "encoder_gpio"
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sic/input/encoder.h"
#include "sic/bus/gpio_bus.h"
#include "sic/sic.h"

typedef struct {
    int pin_a, pin_b, pin_btn;
    int last_a;         /* last sampled A/B state */
    int accum;          /* sub-detent accumulator for full 4-step encoders */
    uint8_t init_done;
} enc_gpio_t;

/* Gray-code half-step table: [prev_a][prev_b][cur_a][cur_b] → step */
static const int8_t k_enc_table[4][4] = {
    /* cur: 00  01  10  11 */
    {  0,  -1, +1,  0 },   /* prev 00 */
    { +1,   0,  0, -1 },   /* prev 01 */
    { -1,   0,  0, +1 },   /* prev 10 */
    {  0,  +1, -1,  0 },   /* prev 11 */
};

static int enc_hw_init(enc_gpio_t* e) {
    if (!e) return -1;
    if (e->init_done) return 0;
    sic_gpio_mode_pullup(e->pin_a);
    sic_gpio_mode_pullup(e->pin_b);
    if (e->pin_btn != SIC_NOPIN)
        sic_gpio_mode_pullup(e->pin_btn);
    e->last_a = (sic_gpio_read(e->pin_a) ? 2 : 0)
              | (sic_gpio_read(e->pin_b) ? 1 : 0);
    e->init_done = 1;
    return 0;
}

static int enc_read_delta(const void* self) {
    const encoder_t* enc = (const encoder_t*)self;
    enc_gpio_t* e = enc ? (enc_gpio_t*)enc->impl : NULL;
    if (enc_hw_init(e) < 0) return 0;

    int a = sic_gpio_read(e->pin_a) ? 1 : 0;
    int b = sic_gpio_read(e->pin_b) ? 1 : 0;
    int prev = e->last_a;  /* encodes last (a<<1|b) state */
    int cur  = (a << 1) | b;

    if (cur != prev) {
        e->accum += k_enc_table[prev][cur];
        e->last_a = cur;
    }

    /* Emit one click per 4 half-steps (standard EC11 full-step detent) */
    int clicks = e->accum / 4;
    e->accum  -= clicks * 4;
    return clicks;
}

static int enc_read_btn(const void* self) {
    const encoder_t* enc = (const encoder_t*)self;
    enc_gpio_t* e = enc ? (enc_gpio_t*)enc->impl : NULL;
    if (enc_hw_init(e) < 0) return -1;
    if (e->pin_btn == SIC_NOPIN) return -1;
    return sic_gpio_read(e->pin_btn) ? 0 : 1;  /* active-low button */
}

static const struct encoder_vtbl_s ENC_VT = { enc_read_delta, enc_read_btn };

/* ---- probe / make ---- */
static const struct encoder_s* make(const sic_encoder_cfg_t* cfg) {
    if (!cfg) return NULL;

    enc_gpio_t* st = (enc_gpio_t*)calloc(1, sizeof *st);
    if (!st) return NULL;
    st->pin_a   = cfg->pin_a;
    st->pin_b   = cfg->pin_b;
    st->pin_btn = cfg->pin_btn;
    encoder_t* k = (encoder_t*)calloc(1, sizeof *k);
    if (!k) { free(st); return NULL; }
    k->v    = &ENC_VT;
    k->impl = st;
    return k;
}

static int probe(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint) return -1;
    if (strcmp(d->hint, "encoder_gpio") != 0) return -1;
    *out = (void*)make((const sic_encoder_cfg_t*)d->cfg);
    return (*out) ? 0 : -1;
}

static const sic_driver_t DRV = {
    .name     = "encoder_gpio",
    .function = SIC_F_ENCODER,
    .probe    = probe,
    .remove   = NULL
};

#ifdef __cplusplus
extern "C" {
#endif
void sic_register_driver_encoder_gpio(void) { sic_registry_register(&DRV); }
#ifdef __cplusplus
}
#endif
