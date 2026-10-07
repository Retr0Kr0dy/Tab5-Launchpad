#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle ----------------------------------------------------------- */
typedef struct { int init_buses; int lazy_drivers; } sic_begin_opts_t;
int  sic_begin(const void* board_desc, const sic_begin_opts_t* opts);
void sic_end(void);

/* ---- system info --------------------------------------------------------- */
typedef struct {
  const char* chip_model;
  uint32_t    chip_rev, cpu_mhz;
  uint32_t    flash_bytes, flash_hz, psram_bytes;
  uint8_t     mac[6];
} sic_sysinfo_t;
int  sic_sysinfo(sic_sysinfo_t* out);

/* ---- capabilities -------------------------------------------------------- */
typedef enum {
  SIC_CAP_PWR_SW=0, SIC_CAP_AUDIO_AMP, SIC_CAP_MIC,
  SIC_CAP_CHARGER, SIC_CAP_KSCAN, SIC_CAP_IR_TX, SIC_CAP_SD,
  SIC_CAP_TOUCH, SIC_CAP_IMU, SIC_CAP_RTC, SIC_CAP_CAMERA,
  SIC_CAP_COUNT
} sic_cap_t;
int  sic_has(sic_cap_t cap);       /* 1 if present */
int  sic_count_cap(sic_cap_t cap); /* number of instances */

/* ---- keyboard: abstract keycodes & events ------------------------------- */
typedef enum {
  SIC_KEY_NONE=0,
  SIC_KEY_BACKSPACE=0x08, SIC_KEY_TAB=0x09, SIC_KEY_ENTER=0x0D, SIC_KEY_SPACE=0x20,
  SIC_KEY_ESC=0x100, SIC_KEY_LEFT, SIC_KEY_RIGHT, SIC_KEY_UP, SIC_KEY_DOWN,
  SIC_KEY_DEL, SIC_KEY_HOME, SIC_KEY_END, SIC_KEY_PGUP, SIC_KEY_PGDN,
  SIC_KEY_F1, SIC_KEY_F2, SIC_KEY_F3, SIC_KEY_F4, SIC_KEY_F5, SIC_KEY_F6,
  SIC_KEY_F7, SIC_KEY_F8, SIC_KEY_F9, SIC_KEY_F10, SIC_KEY_F11, SIC_KEY_F12,
  SIC_KEY_SHIFT, SIC_KEY_CTRL, SIC_KEY_ALT, SIC_KEY_OPT, SIC_KEY_FN, SIC_KEY_CAPS
} sic_keycode_t;

typedef struct {
  sic_keycode_t code;
  uint8_t       pressed;     /* 1=down, 0=up */
  uint8_t       shift:1, ctrl:1, alt:1, opt:1, fn:1, caps:1;
  char          ascii;       /* 0 if not printable */
} sic_key_event_t;

int  sic_key_poll(sic_key_event_t* out); /* 1 ev, 0 none, <0 error */
int  sic_readline(char* buf, int maxlen, int timeout_ms);

/* ---- audio --------------------------------------------------------------- */
#include "audio.h"

/* ---- misc ---------------------------------------------------------------- */
int  sic_ir_send_nec(uint32_t code);   /* 0 ok */
int  sic_sd_present(void);             /* 1 present */

/* ---- I2C (bus 0 helpers) ------------------------------------------------ */
int  sic_i2c_begin(int sda, int scl, uint32_t hz);
int  sic_i2c_scan(uint8_t* addrs, int max);

/* ---- timing -------------------------------------------------------------- */
void         sic_delay_ms(uint32_t ms);
unsigned long sic_millis(void);

#ifdef __cplusplus
}
#endif
