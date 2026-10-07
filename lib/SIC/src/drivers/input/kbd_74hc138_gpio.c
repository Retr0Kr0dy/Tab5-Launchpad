#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sic/input/kscan.h"
#include "sic/bus/gpio_bus.h"
#include "sic/sic_board.h"
#include "sic/sic_registry.h"
#include "sic/bus/delay.h"
#include "sic/log.h"

#ifndef KBD_SEL0
#  ifdef KBD_A0
#    define KBD_SEL0 KBD_A0
#  else
#    define KBD_SEL0 0xFF
#  endif
#endif
#ifndef KBD_SEL1
#  ifdef KBD_A1
#    define KBD_SEL1 KBD_A1
#  else
#    define KBD_SEL1 0xFF
#  endif
#endif
#ifndef KBD_SEL2
#  ifdef KBD_A2
#    define KBD_SEL2 KBD_A2
#  else
#    define KBD_SEL2 0xFF
#  endif
#endif
#ifndef KBD_EN_N
#  ifdef KBD_EN
#    define KBD_EN_N KBD_EN
#  else
#    define KBD_EN_N 0xFF
#  endif
#endif

#ifndef KBD_IN0
#define KBD_IN0 0xFF
#endif
#ifndef KBD_IN1
#define KBD_IN1 0xFF
#endif
#ifndef KBD_IN2
#define KBD_IN2 0xFF
#endif
#ifndef KBD_IN3
#define KBD_IN3 0xFF
#endif
#ifndef KBD_IN4
#define KBD_IN4 0xFF
#endif
#ifndef KBD_IN5
#define KBD_IN5 0xFF
#endif
#ifndef KBD_IN6
#define KBD_IN6 0xFF
#endif
#ifndef KBD_IN7
#define KBD_IN7 0xFF
#endif

#ifndef KBD_ACTIVE_LOW
#define KBD_ACTIVE_LOW 1
#endif
#ifndef SIC_KBD_PULL_UP
#define SIC_KBD_PULL_UP 1
#endif
#ifndef SIC_KBD_PULL_DOWN
#define SIC_KBD_PULL_DOWN 2
#endif
#ifndef UP
#define UP SIC_KBD_PULL_UP
#endif
#ifndef DOWN
#define DOWN SIC_KBD_PULL_DOWN
#endif
#ifndef KBD_PULL
#define KBD_PULL SIC_KBD_PULL_UP
#endif
#ifndef KBD_DEBOUNCE_MS
#define KBD_DEBOUNCE_MS 20
#endif
#ifndef KBD_AUTOSEL
#define KBD_AUTOSEL 0
#endif
#ifndef KBD_DIAG
#define KBD_DIAG 0
#endif
#ifndef KBD_BANK_FLIP
#define KBD_BANK_FLIP 0
#endif
#ifndef KBD_INPUT_SWAP_56
#define KBD_INPUT_SWAP_56 0
#endif
#ifndef KBD_FORCE_ONEHOT
#define KBD_FORCE_ONEHOT 0
#endif
#ifndef KBD_IN_PERM
#define KBD_IN_PERM {0,1,2,3,4,5,6}
#endif

static int ins[8] = {KBD_IN0,KBD_IN1,KBD_IN2,KBD_IN3,KBD_IN4,KBD_IN5,KBD_IN6,KBD_IN7};

static void set_sel(int s){
  sic_gpio_write(KBD_SEL0, (s>>0)&1);
  sic_gpio_write(KBD_SEL1, (s>>1)&1);
  sic_gpio_write(KBD_SEL2, (s>>2)&1);
}
static inline int is_pressed(int v){
#if KBD_ACTIVE_LOW
  return v == 0;
#else
  return v != 0;
#endif
}

typedef struct { uint8_t init_done; } kbd_t;

static int kbd_hw_init(kbd_t* st){
  if (!st) return -1;
  if (st->init_done) return 0;
  if (KBD_SEL0==0xFF || KBD_SEL1==0xFF || KBD_SEL2==0xFF) return -1;
  sic_gpio_mode(KBD_SEL0, 1);
  sic_gpio_mode(KBD_SEL1, 1);
  sic_gpio_mode(KBD_SEL2, 1);
  set_sel(0);
  if (KBD_EN_N!=0xFF){ sic_gpio_mode(KBD_EN_N, 1); sic_gpio_write(KBD_EN_N, 0); }
#if (KBD_PULL==UP)
  for (int i=0;i<8;i++){ if (ins[i]!=0xFF){ sic_gpio_mode_pullup(ins[i]); } }
#else
  for (int i=0;i<8;i++){ if (ins[i]!=0xFF){ sic_gpio_mode_pulldown(ins[i]); } }
#endif
  st->init_done = 1;
  return 0;
}

static int read_bitmap_impl(const struct kscan_s* self, unsigned long long* out_bitmap){
  if (!self || !out_bitmap) return -1;
  kbd_t* st = (kbd_t*)self->impl;
  if (kbd_hw_init(st) < 0) return -1;
  unsigned long long bm = 0ULL;

  static const uint8_t x1[7] = { 0, 2, 4, 6,  8, 10, 12 };
  static const uint8_t x2[7] = { 1, 3, 5, 7,  9, 11, 13 };
  static const uint8_t perm[7] = KBD_IN_PERM;

  for (int i = 0; i < 8; ++i) {
    set_sel(i & 7);
    for (volatile int d=0; d<300; ++d) {}

    uint8_t rawmask = 0;
    for (int j = 0; j < 7; ++j) {
      int p = perm[j]; if (p < 0 || p > 6) continue;
      int pin = ins[p]; if (pin == 0xFF) continue;
      int v = sic_gpio_read(pin);
      if (is_pressed(v)) rawmask |= (1u << j);
    }
#if KBD_INPUT_SWAP_56
    {
      uint8_t b5 = (rawmask >> 5) & 1u;
      uint8_t b6 = (rawmask >> 6) & 1u;
      rawmask &= ~( (1u<<5) | (1u<<6) );
      rawmask |= (b5 << 6) | (b6 << 5);
    }
#endif
    uint8_t mask = rawmask;
#if KBD_FORCE_ONEHOT
    if (mask) mask &= (uint8_t)(- (int8_t)mask);
#endif
#if KBD_DIAG
    if (rawmask) { SIC_LOG(SIC_LOG_DBG, "[KBD] i=%d raw=0x%02X mask=0x%02X", i, rawmask, mask); }
#endif
    if (!mask) continue;

    int row = (i > 3) ? (i - 4) : i;
    row = (-row) + 3;

    for (int j = 0; j < 7; ++j) if (mask & (1u << j)) {
#if KBD_BANK_FLIP
      int col = (i > 3) ? x2[j] : x1[j];
#else
      int col = (i > 3) ? x1[j] : x2[j];
#endif
      int idx = row + col * 4;
      if ((unsigned)idx < 56) bm |= (1ULL << idx);
    }
  }
  *out_bitmap = bm;
  return 0;
}

static int read_key_impl(const void* self_void){
  (void)self_void;
  // Legacy single-key read not used in HAL; return -1 to avoid double-read
  return -1;
}
static const struct kscan_vtbl_s VT = {
    .read_key    = read_key_impl,
    .read_bitmap = read_bitmap_impl
};

static const struct kscan_s* make(const sic_kscan_cfg_t* cfg){
  kbd_t* st = (kbd_t*)calloc(1,sizeof *st); if (!st) return NULL;
  struct kscan_s* k = (struct kscan_s*)calloc(1,sizeof *k); if (!k){ free(st); return NULL; }
  k->impl          = st;
  k->v             = &VT;
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
  return k;
}
static int probe(const void* icdesc, void** out){
  const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
  if (!d || !d->hint) return -1;
  if (strcmp(d->hint,"kbd_74hc138") != 0) return -1;
  *out = (void*)make((const sic_kscan_cfg_t*)d->cfg);
  return (*out)?0:-1;
}
static const sic_driver_t DRV = { .name="kbd_74hc138", .function=SIC_F_KSCAN, .probe=probe, .remove=NULL };
#ifdef __cplusplus
extern "C" { void sic_register_driver_kbd_74hc138(void){ sic_registry_register(&DRV); } }
#else
void sic_register_driver_kbd_74hc138(void){ sic_registry_register(&DRV); }
#endif
