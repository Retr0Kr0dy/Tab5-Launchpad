/*
 * kbd_tca8418_i2c.c — SIC kscan driver for the TCA8418 I2C keyboard controller.
 *
 * Driver contract:
 *   - expose a normalized logical bitmap to SIC, not raw TCA8418 key codes.
 *   - do no hardware I/O in probe(); initialise lazily on first read_bitmap().
 *   - keep board layout knowledge in sic_kscan_cfg_t.scan_to_index/keymaps.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sic/input/kscan.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/gpio_bus.h"
#include "sic/sic_board.h"
#include "sic/sic_registry.h"
#include "sic/sic.h"

/* ---- Build-flag defaults ---- */
#ifndef TCA8418_I2C_BUS
#  define TCA8418_I2C_BUS  0
#endif
#ifndef TCA8418_I2C_ADDR
#  define TCA8418_I2C_ADDR 0x34
#endif
#ifndef TCA8418_INT_PIN
#  define TCA8418_INT_PIN  SIC_NOPIN
#endif
#ifndef TCA8418_ROWS
#  define TCA8418_ROWS 8
#endif
#ifndef TCA8418_COLS
#  define TCA8418_COLS 10
#endif

/* ---- TCA8418 register map ---- */
#define TCA8418_REG_CFG        0x01  /* Configuration */
#define TCA8418_REG_INT_STAT   0x02  /* Interrupt status (write 1 to clear) */
#define TCA8418_REG_KEY_LCK_EC 0x03  /* bits[3:0] = FIFO event count */
#define TCA8418_REG_KEY_EVT_A  0x04  /* First FIFO entry (10 slots: 0x04-0x0D) */
#define TCA8418_REG_KP_GPIO1   0x1D  /* Row  0-7  keypad-vs-GPIO selection (1=keypad) */
#define TCA8418_REG_KP_GPIO2   0x1E  /* Col  0-7  keypad-vs-GPIO selection (1=keypad) */
#define TCA8418_REG_KP_GPIO3   0x1F  /* Col  8-9  keypad-vs-GPIO selection (bits[1:0]) */

/* CFG bits */
#define TCA8418_CFG_KE_IEN   (1u << 0)  /* Key-event interrupt enable */
#define TCA8418_CFG_INT_CFG  (1u << 4)  /* INT pin level: 0=active-low */

/* KEY_EVENT encoding */
#define TCA8418_EVT_PRESS    (1u << 7)
#define TCA8418_EVT_CODE(b)  ((b) & 0x7F)  /* keypad code 1-80 */

/* ---- I2C helpers ---- */
static int reg_read(uint8_t reg, uint8_t* val) {
    return sic_i2c_writeread(TCA8418_I2C_BUS,
                             TCA8418_I2C_ADDR,
                             &reg, 1,
                             val, 1);
}
static int reg_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return sic_i2c_write(TCA8418_I2C_BUS, TCA8418_I2C_ADDR, buf, 2);
}

/* ---- Driver state ---- */
typedef struct {
    unsigned long long state_bm;    /* live logical key state */
    unsigned long long pending_bm;  /* press events accumulated since last read_bitmap call */
    sic_scan_to_index_fn scan_to_index;
    uint8_t rows;
    uint8_t cols;
    uint8_t init_done;
} kbd_tca8418_t;

static uint8_t low_mask_u8(uint8_t n) {
    if (n >= 8) return 0xFFu;
    return (uint8_t)((1u << n) - 1u);
}

static int tca8418_default_scan_to_index(int code) {
    int idx = code - 1;
    return (idx >= 0 && idx < 64) ? idx : -1;
}

static int tca8418_hw_init(kbd_tca8418_t* st) {
    if (st->init_done) return 0;

    uint8_t rows = st->rows ? st->rows : TCA8418_ROWS;
    uint8_t cols = st->cols ? st->cols : TCA8418_COLS;
    if (rows > 8) rows = 8;
    if (cols > 10) cols = 10;

    /* Select only the rows/cols used by this board as keypad pins. */
    if (reg_write(TCA8418_REG_KP_GPIO1, low_mask_u8(rows)) < 0) return -1;
    if (reg_write(TCA8418_REG_KP_GPIO2, low_mask_u8(cols > 8 ? 8 : cols)) < 0) return -1;
    if (reg_write(TCA8418_REG_KP_GPIO3, (cols > 8) ? low_mask_u8((uint8_t)(cols - 8)) : 0x00) < 0) return -1;

    /* Drain stale FIFO after power/reset and clear pending key interrupt. */
    uint8_t ec_reg = 0;
    if (reg_read(TCA8418_REG_KEY_LCK_EC, &ec_reg) == 0) {
        int count = (int)(ec_reg & 0x0F);
        for (int i = 0; i < count; ++i) {
            uint8_t evt = 0;
            (void)reg_read(TCA8418_REG_KEY_EVT_A, &evt);
        }
    }
    (void)reg_write(TCA8418_REG_INT_STAT, 0x01);

    /* Enable key-event interrupt; INT pin is active-low/open-drain on common boards. */
    if (reg_write(TCA8418_REG_CFG, TCA8418_CFG_KE_IEN) < 0) return -1;

    if (TCA8418_INT_PIN != SIC_NOPIN) {
        sic_gpio_mode_pullup(TCA8418_INT_PIN);
    }

    st->init_done = 1;
    return 0;
}

/* ---- kscan_read_bitmap implementation ---- */
static int tca8418_read_bitmap(const struct kscan_s* self, unsigned long long* out) {
    if (!self || !out) return -1;
    kbd_tca8418_t* st = (kbd_tca8418_t*)self->impl;
    if (!st) return -1;
    if (tca8418_hw_init(st) < 0) return -1;

    /* Check whether an event is pending (skip I2C drain if INT pin is high). */
    if (TCA8418_INT_PIN != SIC_NOPIN && sic_gpio_read(TCA8418_INT_PIN) != 0) {
        *out = st->state_bm | st->pending_bm;
        st->pending_bm = 0;
        return 0;
    }

    /* Drain the event FIFO. */
    uint8_t ec_reg = 0;
    if (reg_read(TCA8418_REG_KEY_LCK_EC, &ec_reg) < 0) return -1;
    int count = (int)(ec_reg & 0x0F);

    for (int i = 0; i < count; ++i) {
        uint8_t evt = 0;
        if (reg_read(TCA8418_REG_KEY_EVT_A, &evt) < 0) break;
        int code = (int)TCA8418_EVT_CODE(evt);
        if (code < 1 || code > 80) continue;

        int idx = st->scan_to_index ? st->scan_to_index(code) : tca8418_default_scan_to_index(code);
        if (idx < 0 || idx >= 64) continue;
        unsigned long long bit = 1ULL << idx;

        if (evt & TCA8418_EVT_PRESS) {
            st->state_bm   |= bit;
            st->pending_bm |= bit; /* latch press — survives same-poll release */
        } else {
            st->state_bm   &= ~bit;
        }
    }

    /* Clear K_INT in INT_STAT (write-1-to-clear). */
    (void)reg_write(TCA8418_REG_INT_STAT, 0x01);

    *out = st->state_bm | st->pending_bm;
    st->pending_bm = 0;
    return 0;
}

/* ---- vtable ---- */
static int read_key_stub(const void* self) { (void)self; return -1; }
static const struct kscan_vtbl_s TCA8418_VT = {
    .read_key    = read_key_stub,
    .read_bitmap = tca8418_read_bitmap,
};

static void copy_cfg(struct kscan_s* k, kbd_tca8418_t* st, const sic_kscan_cfg_t* cfg) {
    k->keymap        = cfg ? cfg->keymap        : NULL;
    k->keymap_alt    = cfg ? cfg->keymap_alt    : NULL;
    k->keymap_fn     = cfg ? cfg->keymap_fn     : NULL;
    k->keycode       = cfg ? cfg->keycode       : NULL;
    k->keycode_alt   = cfg ? cfg->keycode_alt   : NULL;
    k->keycode_fn    = cfg ? cfg->keycode_fn    : NULL;
    k->modifier_mask = cfg ? cfg->modifier_mask : 0;
    k->shift_mask    = cfg ? cfg->shift_mask    : 0;
    k->ctrl_mask     = cfg ? cfg->ctrl_mask     : 0;
    k->opt_mask      = cfg ? cfg->opt_mask      : 0;
    k->fn_mask       = cfg ? cfg->fn_mask       : 0;
    k->caps_mask     = cfg ? cfg->caps_mask     : 0;
    k->scan_to_index = cfg ? cfg->scan_to_index : NULL;
    k->rows          = cfg ? cfg->rows          : 0;
    k->cols          = cfg ? cfg->cols          : 0;

    st->scan_to_index = k->scan_to_index;
    st->rows          = k->rows;
    st->cols          = k->cols;
}

/* ---- Probe / make ---- */
static const struct kscan_s* make(const sic_kscan_cfg_t* cfg) {
    kbd_tca8418_t* st = (kbd_tca8418_t*)calloc(1, sizeof *st);
    if (!st) return NULL;

    struct kscan_s* k = (struct kscan_s*)calloc(1, sizeof *k);
    if (!k) { free(st); return NULL; }

    k->v    = &TCA8418_VT;
    k->impl = st;
    copy_cfg(k, st, cfg);
    return k;
}

static int probe(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || !out) return -1;
    if (strcmp(d->hint, "kbd_tca8418") != 0) return -1;
    *out = (void*)make((const sic_kscan_cfg_t*)d->cfg);
    return (*out) ? 0 : -1;
}

static const sic_driver_t DRV = {
    .name     = "kbd_tca8418",
    .function = SIC_F_KSCAN,
    .probe    = probe,
    .remove   = NULL
};

#ifdef __cplusplus
extern "C" {
#endif
void sic_register_driver_kbd_tca8418(void) { sic_registry_register(&DRV); }
#ifdef __cplusplus
}
#endif
