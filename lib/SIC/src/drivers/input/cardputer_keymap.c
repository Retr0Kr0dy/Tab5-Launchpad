#include <stdint.h>
#include "sic/hal.h"

/*
 * M5Stack Cardputer logical keymap.
 *
 * Logical index is column-major to match the original 74HC138 scanner:
 *   idx = row + col * 4, row=[0..3], col=[0..13]
 *
 * This matches M5Cardputer's public _key_value_map[4][14].  The same logical
 * map is reused by the original/v1.1 GPIO matrix and the ADV TCA8418 scanner;
 * each driver is responsible for normalizing its physical scan codes into this
 * index space.
 */

enum {
    CARDPUTER_IDX_FN    = 2,   /* row 2 col 0 */
    CARDPUTER_IDX_SHIFT = 6,   /* row 2 col 1 */
    CARDPUTER_IDX_CTRL  = 3,   /* row 3 col 0 */
    CARDPUTER_IDX_OPT   = 7,   /* row 3 col 1 */
    CARDPUTER_IDX_ALT   = 11,  /* row 3 col 2 */
};

char sic_keymap_cardputer(int idx){
  static const char map[4][14] = {
    { '`','1','2','3','4','5','6','7','8','9','0','-','=', '\b' },
    { '\t','q','w','e','r','t','y','u','i','o','p','[',']','\\' },
    { 0,   0,  'a','s','d','f','g','h','j','k','l',';','\'','\r' },
    { 0,   0,   0, 'z','x','c','v','b','n','m',',','.','/',' '  }
  };
  if (idx < 0 || idx >= 56) return 0;
  int row = idx & 3;
  int col = idx >> 2;
  return map[row][col];
}

sic_keycode_t sic_keycode_cardputer(int idx){
  switch (idx) {
    case CARDPUTER_IDX_FN:    return SIC_KEY_FN;
    case CARDPUTER_IDX_SHIFT: return SIC_KEY_SHIFT;
    case CARDPUTER_IDX_CTRL:  return SIC_KEY_CTRL;
    case CARDPUTER_IDX_OPT:   return SIC_KEY_OPT;
    case CARDPUTER_IDX_ALT:   return SIC_KEY_ALT;
    default:                  return SIC_KEY_NONE;
  }
}

sic_keycode_t sic_keycode_cardputer_fn(int idx){
  /* Fn layer based on the printed keycap conventions used by M5Cardputer:
   * Fn+` = Esc, Fn+Backspace = forward Delete, Fn+1..0 = F1..F10,
   * Fn+; , . / = arrow cluster. */
  switch (idx) {
    case 0:  return SIC_KEY_ESC;   /* ` */
    case 4:  return SIC_KEY_F1;    /* 1 */
    case 8:  return SIC_KEY_F2;    /* 2 */
    case 12: return SIC_KEY_F3;    /* 3 */
    case 16: return SIC_KEY_F4;    /* 4 */
    case 20: return SIC_KEY_F5;    /* 5 */
    case 24: return SIC_KEY_F6;    /* 6 */
    case 28: return SIC_KEY_F7;    /* 7 */
    case 32: return SIC_KEY_F8;    /* 8 */
    case 36: return SIC_KEY_F9;    /* 9 */
    case 40: return SIC_KEY_F10;   /* 0 */
    case 52: return SIC_KEY_DEL;   /* Backspace */
    case 46: return SIC_KEY_UP;    /* ; */
    case 43: return SIC_KEY_LEFT;  /* , */
    case 47: return SIC_KEY_DOWN;  /* . */
    case 51: return SIC_KEY_RIGHT; /* / */
    default: return SIC_KEY_NONE;
  }
}

/* TCA8418 physical scan-code normalizer for Cardputer ADV.
 *
 * M5Stack's official M5Cardputer ADV reader calls `matrix(7, 8)` and then
 * remaps the TCA8418 raw event as:
 *   raw_row = (code-1) / 10;     // 0..6
 *   raw_col = (code-1) % 10;     // 0..7 used
 *   logical_col = raw_row * 2 + (raw_col > 3);
 *   logical_row = (raw_col + 4) % 4;
 *
 * SIC logical indices are row + col*4, so this returns the same 4x14 logical
 * space as the original Cardputer 74HC138 scanner without leaking TCA8418
 * details into the HAL or examples.
 */
int sic_cardputer_tca8418_scan_to_index(int scan_code){
  if (scan_code < 1) return -1;
  int raw = scan_code - 1;
  int tca_row = raw / 10;
  int tca_col = raw % 10;
  if (tca_row < 0 || tca_row >= 7 || tca_col < 0 || tca_col >= 8) return -1;

  int logical_col = (tca_row * 2) + ((tca_col > 3) ? 1 : 0);
  int logical_row = (tca_col + 4) % 4;
  int idx = logical_row + logical_col * 4;
  return (idx >= 0 && idx < 56) ? idx : -1;
}
