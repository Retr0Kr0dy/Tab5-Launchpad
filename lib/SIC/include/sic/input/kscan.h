#pragma once
#include <stdint.h>
#include "sic/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Keyboard scanning abstraction.
 *
 * Driver responsibility:
 *   - convert whatever the silicon exposes (GPIO matrix position, FIFO code,
 *     expander bit, etc.) into a stable logical bitmap.
 *   - bit N means logical key index N is currently down.
 *
 * Board/keymap responsibility:
 *   - describe which logical bits are modifiers/layer keys.
 *   - map logical key indices to ASCII and/or SIC keycodes.
 *
 * Application responsibility:
 *   - consume sic_key_event_t from sic_key_poll(); do not know the scanner IC.
 */

/* Legacy special character emitted by old alt maps to request caps toggle.
 * New code should prefer caps_mask, but this remains supported for compatibility. */
#define SIC_KEY_CAPS_LOCK '\x0E'

/* Logical keyboard modifier/layer bits returned in sic_key_event_t flags. */
typedef enum {
    SIC_KMOD_SHIFT = 1u << 0,
    SIC_KMOD_CTRL  = 1u << 1,
    SIC_KMOD_ALT   = 1u << 2,
    SIC_KMOD_OPT   = 1u << 3,
    SIC_KMOD_FN    = 1u << 4,
    SIC_KMOD_CAPS  = 1u << 5,
} sic_kmod_t;

typedef char (*sic_keymap_fn)(int idx);
typedef sic_keycode_t (*sic_keycode_map_fn)(int idx);
/* Optional physical scan-code normalizer. Input is driver-specific but should
 * usually be the hardware code, not the already-normalized bitmap index.
 * Return logical index [0,63], or negative to ignore the key. */
typedef int (*sic_scan_to_index_fn)(int scan_code);

/* Board-supplied keyboard configuration passed via sic_board_ic_t.cfg. */
typedef struct sic_kscan_cfg_s {
    sic_keymap_fn      keymap;        /* base layer: index -> ASCII, NULL = none */
    sic_keymap_fn      keymap_alt;    /* alt/orange layer: index -> ASCII */
    sic_keymap_fn      keymap_fn;     /* fn layer printable fallback, optional */

    sic_keycode_map_fn keycode;       /* base special key map, optional */
    sic_keycode_map_fn keycode_alt;   /* alt/orange special key map, optional */
    sic_keycode_map_fn keycode_fn;    /* fn special key map, optional */

    unsigned long long modifier_mask; /* alt/orange modifier bit(s); consumed */
    unsigned long long shift_mask;    /* shift bit(s); consumed */
    unsigned long long ctrl_mask;     /* ctrl bit(s); consumed */
    unsigned long long opt_mask;      /* option/meta bit(s); consumed */
    unsigned long long fn_mask;       /* fn layer bit(s); consumed */
    unsigned long long caps_mask;     /* caps-lock toggle bit(s); consumed */

    sic_scan_to_index_fn scan_to_index; /* optional driver scan-code -> logical index */
    uint8_t rows;                     /* optional physical rows for matrix ICs; 0=driver default */
    uint8_t cols;                     /* optional physical cols for matrix ICs; 0=driver default */
} sic_kscan_cfg_t;

struct kscan_s; /* forward declaration — defined below */

struct kscan_vtbl_s {
    int (*read_key)(const void*); /* legacy single-key path; may be NULL/unsupported */
    /* read_bitmap: fill *out with the current key-down bitmask.
     * Bit N = logical key index N is pressed. Returns 0 on success, <0 on error. */
    int (*read_bitmap)(const struct kscan_s* self, unsigned long long* out);
};

typedef struct kscan_s {
    const struct kscan_vtbl_s* v;
    void* impl;

    sic_keymap_fn      keymap;
    sic_keymap_fn      keymap_alt;
    sic_keymap_fn      keymap_fn;
    sic_keycode_map_fn keycode;
    sic_keycode_map_fn keycode_alt;
    sic_keycode_map_fn keycode_fn;

    unsigned long long modifier_mask;
    unsigned long long shift_mask;
    unsigned long long ctrl_mask;
    unsigned long long opt_mask;
    unsigned long long fn_mask;
    unsigned long long caps_mask;

    sic_scan_to_index_fn scan_to_index;
    uint8_t rows;
    uint8_t cols;
} kscan_t;

const kscan_t* sic_kbd(int index);
/* Dispatches to self->v->read_bitmap — defined in kscan_dispatch.c */
int kscan_read_bitmap(const struct kscan_s* self, unsigned long long* out_bitmap);

#ifdef __cplusplus
}
#endif
