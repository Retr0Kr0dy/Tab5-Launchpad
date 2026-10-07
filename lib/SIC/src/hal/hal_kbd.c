#include <string.h>
#include <stdint.h>
#include "sic/hal.h"
#include "sic/input/kscan.h"
#include "sic/bus/delay.h"

/*
 * Public keyboard event layer.
 *
 * This file must not know about Cardputer, T-Pager, TCA8418, 74HC138, or any
 * other board/IC detail.  Drivers expose a normalized bitmap; board configs
 * expose modifier masks and keymaps.  The HAL turns that into portable events.
 */

#ifndef SIC_KBD_EVENT_QUEUE_LEN
#define SIC_KBD_EVENT_QUEUE_LEN 16
#endif

typedef struct {
  uint64_t prev_bm;
  uint8_t  caps_on;
  sic_key_event_t q[SIC_KBD_EVENT_QUEUE_LEN];
  uint8_t q_head;
  uint8_t q_tail;
} sic_kbd_state_t;

/* SIC currently exposes sic_key_poll() for kbd index 0.  Keep state local to
 * that public path; if indexed polling is added later this becomes per-device. */
static sic_kbd_state_t g_kbd0;

static int q_empty(void){ return g_kbd0.q_head == g_kbd0.q_tail; }
static int q_push(const sic_key_event_t* ev){
  uint8_t n = (uint8_t)((g_kbd0.q_head + 1u) % SIC_KBD_EVENT_QUEUE_LEN);
  if (n == g_kbd0.q_tail) return 0; /* drop newest instead of corrupting order */
  g_kbd0.q[g_kbd0.q_head] = *ev;
  g_kbd0.q_head = n;
  return 1;
}
static int q_pop(sic_key_event_t* ev){
  if (q_empty()) return 0;
  *ev = g_kbd0.q[g_kbd0.q_tail];
  g_kbd0.q_tail = (uint8_t)((g_kbd0.q_tail + 1u) % SIC_KBD_EVENT_QUEUE_LEN);
  return 1;
}

static char shiftify_symbol(char c){
  switch(c){
    case '`': return '~';
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    default:  return c;
  }
}

static char translate_ascii(char c, int shift, int caps){
  if (c >= 'a' && c <= 'z') {
    return (shift ^ caps) ? (char)(c - 'a' + 'A') : c;
  }
  return shift ? shiftify_symbol(c) : c;
}

static char ctrlify_ascii(char c){
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 1);
  if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 1);
  switch (c) {
    case '[': return 0x1b;
    case '\\': return 0x1c;
    case ']': return 0x1d;
    case '^': return 0x1e;
    case '_': return 0x1f;
    default: return c;
  }
}

static int in_mask(const kscan_t* k, int idx, uint64_t mask){
  (void)k;
  return (idx >= 0 && idx < 64 && (mask & (1ULL << idx)) != 0);
}

static sic_keycode_t modifier_code_for_idx(const kscan_t* k, int idx){
  if (in_mask(k, idx, k->shift_mask))    return SIC_KEY_SHIFT;
  if (in_mask(k, idx, k->ctrl_mask))     return SIC_KEY_CTRL;
  if (in_mask(k, idx, k->modifier_mask)) return SIC_KEY_ALT;
  if (in_mask(k, idx, k->opt_mask))      return SIC_KEY_OPT;
  if (in_mask(k, idx, k->fn_mask))       return SIC_KEY_FN;
  if (in_mask(k, idx, k->caps_mask))     return SIC_KEY_CAPS;
  return SIC_KEY_NONE;
}

static void fill_mods(sic_key_event_t* ev, const kscan_t* k, uint64_t bm){
  ev->shift = (bm & k->shift_mask)    ? 1u : 0u;
  ev->ctrl  = (bm & k->ctrl_mask)     ? 1u : 0u;
  ev->alt   = (bm & k->modifier_mask) ? 1u : 0u;
  ev->opt   = (bm & k->opt_mask)      ? 1u : 0u;
  ev->fn    = (bm & k->fn_mask)       ? 1u : 0u;
  ev->caps  = g_kbd0.caps_on          ? 1u : 0u;
}

static sic_keycode_t code_from_ascii(char c){
  switch (c) {
    case '\b': return SIC_KEY_BACKSPACE;
    case 0x7f: return SIC_KEY_DEL;
    case '\t': return SIC_KEY_TAB;
    case '\n': return SIC_KEY_ENTER;
    case '\r': return SIC_KEY_ENTER;
    case ' ':  return SIC_KEY_SPACE;
    case 0x1b: return SIC_KEY_ESC;
    default:   return c ? (sic_keycode_t)(unsigned char)c : SIC_KEY_NONE;
  }
}

static sic_keycode_t map_code(const kscan_t* k, int idx, int fn, int alt){
  sic_keycode_t code = SIC_KEY_NONE;
  if (fn && k->keycode_fn)       code = k->keycode_fn(idx);
  if (code == SIC_KEY_NONE && alt && k->keycode_alt) code = k->keycode_alt(idx);
  if (code == SIC_KEY_NONE && k->keycode)            code = k->keycode(idx);
  return code;
}

static char map_ascii(const kscan_t* k, int idx, int fn, int alt, int shift, int caps){
  char c = 0;
  if (fn && k->keymap_fn) c = k->keymap_fn(idx);
  if (!c && alt && k->keymap_alt) c = k->keymap_alt(idx);
  if (!c && k->keymap) {
    c = k->keymap(idx);
    if (c) c = translate_ascii(c, shift, caps);
  }
  return c;
}

static void build_event(const kscan_t* k, int idx, int pressed, uint64_t bm_after,
                        sic_key_event_t* ev){
  memset(ev, 0, sizeof *ev);
  ev->pressed = (uint8_t)pressed;
  fill_mods(ev, k, bm_after);

  sic_keycode_t mod_code = modifier_code_for_idx(k, idx);
  if (mod_code != SIC_KEY_NONE) {
    ev->code = mod_code;
    return;
  }

  int fn_held    = (bm_after & k->fn_mask)       != 0;
  int alt_held   = (bm_after & k->modifier_mask) != 0;
  int shift_held = (bm_after & k->shift_mask) != 0;
  int ctrl_held  = (bm_after & k->ctrl_mask)  != 0;

  ev->code = map_code(k, idx, fn_held, alt_held);
  ev->ascii = map_ascii(k, idx, fn_held, alt_held, shift_held, g_kbd0.caps_on);
  if (ctrl_held && ev->ascii) ev->ascii = ctrlify_ascii(ev->ascii);

  if (ev->ascii == SIC_KEY_CAPS_LOCK) {
    g_kbd0.caps_on ^= 1u;
    ev->ascii = 0;
    ev->code = SIC_KEY_CAPS;
    ev->caps = g_kbd0.caps_on;
    return;
  }

  if (ev->code == SIC_KEY_NONE) ev->code = code_from_ascii(ev->ascii);
}

static void enqueue_scan_events(const kscan_t* k, uint64_t bm){
  uint64_t changed = bm ^ g_kbd0.prev_bm;
  if (!changed) return;

  /* Presses first, then releases.  This makes chords like Shift+A visible as
   * modifier-down before printable-down even if they appear in one scan. */
  for (int pass = 0; pass < 2; ++pass) {
    for (int idx = 0; idx < 64; ++idx) {
      uint64_t bit = 1ULL << idx;
      if (!(changed & bit)) continue;
      int pressed = (bm & bit) != 0;
      if ((pass == 0 && !pressed) || (pass == 1 && pressed)) continue;

      /* caps_mask is a toggle key; it is consumed like a modifier, but unlike
       * Shift/Fn/Alt it has state that survives release. */
      if (pressed && in_mask(k, idx, k->caps_mask)) g_kbd0.caps_on ^= 1u;

      sic_key_event_t ev;
      build_event(k, idx, pressed, bm, &ev);
      q_push(&ev);
    }
  }
  g_kbd0.prev_bm = bm;
}

int sic_key_poll(sic_key_event_t* ev){
  if (!ev) return -1;
  if (q_pop(ev)) return 1;

  const kscan_t* k = sic_kbd(0);
  if (!k || !k->v || !k->v->read_bitmap) return -1;

  unsigned long long bm = 0;
  if (kscan_read_bitmap(k, &bm) != 0) return 0;
  enqueue_scan_events(k, (uint64_t)bm);
  return q_pop(ev) ? 1 : 0;
}

int sic_readline(char* buf, int maxlen, int timeout_ms){
  if (!buf || maxlen<=1) return -1;
  int n=0;
  unsigned long t0 = sic_millis();
  for(;;){
    sic_key_event_t ev;
    if (sic_key_poll(&ev)>0 && ev.pressed){
      if (ev.code==SIC_KEY_ENTER){ buf[n]=0; return n; }
      else if (ev.code==SIC_KEY_BACKSPACE){ if (n>0) n--; }
      else if (ev.ascii && n<maxlen-1){ buf[n++]=ev.ascii; }
    }
    if (timeout_ms>0 && (int)(sic_millis()-t0) > timeout_ms){ buf[n]=0; return n; }
    sic_delay_ms(1);
  }
}
