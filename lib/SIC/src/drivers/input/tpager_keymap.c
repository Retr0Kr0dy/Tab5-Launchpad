/*
 * tpager_keymap.c — ASCII keymap for the LilyGo T-Pager physical keyboard.
 *
 * The keyboard is scanned by a TCA8418 (I2C 0x34).  TCA8418 key codes are
 * 1-based: code = (row * 10) + col + 1, for row 0-7 and col 0-9.
 *
 * The bitmap index passed to keymap() = code - 1  (0-based).
 *
 * !! PROVISIONAL LAYOUT !!
 * The exact row/col-to-key mapping depends on PCB wiring.  Verify this table
 * by pressing each physical key and checking which idx is reported, then adjust
 * the array entries accordingly.  The layout below is a best-effort starting
 * point based on the T-Pager's visible key arrangement.
 *
 * Special keys:
 *   '\x08'  = Backspace
 *   '\r'    = Enter / Return
 *   '\x01'  = Shift  (SIC_KEY_SHIFT will be mapped from is_modifier_idx)
 *   '\x02'  = Symbol / Alt
 *   '\x11'  = Ctrl (currently handled by is_modifier_idx index 1)
 *   ' '     = Space
 */

#include <stddef.h>
#include "sic/input/kscan.h"

/*
 * Index layout (key code 1 → idx 0, code 80 → idx 79):
 *
 * idx  0- 9 : row 0  — Q  W  E  R  T  Y  U  I  O  P
 * idx 10-19 : row 1  — A  S  D  F  G  H  J  K  L  En
 * idx 20-29 : row 2  — Or Z  X  C  V  B  N  M  CAP BS
 * idx 30-39 : row 3  — SP  ?  ?  ?  ?  ?  ?  ?  ?  ?
 * idx 40-63 : rows 4-6  (reserved / unmapped on this keyboard)
 * idx 64-79 : row 6-7  (not present on T-Pager hardware)
 *
 * Keys mapped to '\0' are not present or not yet confirmed.
 */
static const char g_map[64] = {
    /* row 0  col 0-9 */
    'q','w','e','r','t','y','u','i','o','p',
    /* row 1  col 0-9 */
    'a','s','d','f','g','h','j','k','l','\r',
    /* row 2  col 0-9  (col 0=Orange modifier → 0, col 8=Shift/CAP modifier → 0) */
    0,'z','x','c','v','b','n','m',0,'\x08',
    /* row 3  col 0-9  (col 0=Space confirmed at bit 30; rest TBD) */
    ' ',0,0,0,0,0,0,0,0,0,
    /* rows 4-5  (not wired on T-Pager) */
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    /* row 6 col 0-3 */
    0,0,0,0
};

char sic_keymap_tpager(int idx) {
    if (idx < 0 || idx >= (int)(sizeof g_map / sizeof g_map[0])) return 0;
    return g_map[idx];
}

/*
 * Orange (alt) layer — numbers and symbols accessible via Orange + key.
 *
 * !! PROVISIONAL !!  Verify by holding Orange and pressing each key, then
 * adjusting the array.  Layout modelled on T-Pager PCB silk-screen markings.
 *
 * idx  0- 9 : Orange + row 0  — 1  2  3  4  5  6  7  8  9  0
 * idx 10-19 : Orange + row 1  — *  /  +  -  =  :  '  "  @  (enter)
 * idx 20-29 : Orange + row 2  — [orange:no char]  _  $  ;  ?  !  `  [?]  CAPLOCK  BS
 * idx 30-39 : Orange + row 3  — [space]  ...
 */
static const char g_map_alt[64] = {
    /* row 0 + Orange: numbers */
    '1','2','3','4','5','6','7','8','9','0',
    /* row 1 + Orange: symbols */
    '*','/','+','-','=',':','\'','"','@','\r',
    /* row 2 + Orange  (idx 20 = Orange key itself → no char; idx 28 = CAPLOCK; idx 29 = BS) */
    0,'_','$',';','?','!','`','.',SIC_KEY_CAPS_LOCK,'\x08',
    /* row 3 + Orange  (col 0 = Space) */
    ' ',0,0,0,0,0,0,0,0,0,
    /* rows 4-5 (not wired) */
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    /* row 6 col 0-3 */
    0,0,0,0
};

char sic_keymap_tpager_alt(int idx) {
    if (idx < 0 || idx >= (int)(sizeof g_map_alt / sizeof g_map_alt[0])) return 0;
    return g_map_alt[idx];
}
