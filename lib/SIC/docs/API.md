# SIC API Reference

All devices are accessed through typed vtable pointers returned by index accessors.
Index 0 = first device of that type registered for the current board.

## Core

```c
#include <sic/sic.h>

/* Initialise SIC for a specific board. Returns 1 on success, 0 if no board. */
int sic_begin(const sic_board_t* board, const sic_begin_opts_t* opts);
```

Boards are declared in `src/boards/` and selected via build flags (e.g. `-DSIC_TARGET_TPAGER=1`).

---

## Keyboard

High-level applications should use `sic_key_poll()`. It returns abstract key
events with portable keycodes, printable ASCII, and modifier flags. The caller
never needs to know whether the board uses a GPIO matrix, TCA8418, or another
scanner.

```c
#include <sic/sic.h>

sic_key_event_t ev;
if (sic_key_poll(&ev) > 0 && ev.pressed) {
    if (ev.ascii) {
        /* printable input */
    } else if (ev.code == SIC_KEY_LEFT) {
        /* navigation */
    }
}
```

`ev.shift`, `ev.ctrl`, `ev.alt`, `ev.opt`, `ev.fn`, and `ev.caps` describe the
logical modifier state after the event. Shift affects symbols and letters; Caps
affects letters only; Ctrl can emit console control bytes such as Ctrl-C while
still exposing `ev.ctrl`. `ev.code` uses `sic_keycode_t`, including navigation
keys, Delete, Escape, F1..F12, and modifier key press/release events.

Low-level keyboard drivers expose a normalized bitmap through `kscan_t`:

```c
#include <sic/input/kscan.h>

const kscan_t* kbd = sic_kbd(0);
uint64_t bm = 0;
if (kbd && kscan_read_bitmap(kbd, &bm) == 0) {
    /* bit N = logical key index N is currently down */
}
```

Use the bitmap path only for board bring-up or diagnostics. Board configs map
hardware scan codes to logical indices via `scan_to_index`, then map logical
indices to ASCII/keycodes with `keymap*` and `keycode*` callbacks.

---

## Rotary Encoder

```c
#include <sic/input/encoder.h>

const encoder_t* sic_encoder(int index);

/* encoder vtable */
int delta = enc->v->read(enc);    /* consume accumulated steps since last call */
int btn   = enc->v->button(enc);  /* 1 = pressed, 0 = released */
```

---

## Microphone

```c
#include <sic/audio/mic.h>

const mic_t* sic_mic(int index);

/* mic vtable */
int rc = mic->v->start(mic, 16000);               /* open I2S + init codec; rc<0 = error */
int n  = mic->v->read(mic, int16_t* buf, int n);  /* returns frames read, <0 = error */
```

---

## Speaker Amp

```c
#include <sic/audio/amp.h>

const amp_t* sic_amp(int index);

/* amp vtable */
amp->v->enable(amp, 1);   /* unmute DAC and power speaker amp */
amp->v->enable(amp, 0);   /* mute */
```

Audio data is sent to the codec via the HAL directly:

```c
#include <sic/audio.h>

int sic_codec_write(const int16_t* mono, int n);  /* mono frames → stereo I2S TX */
```

---

## Battery

```c
#include <sic/sic.h>

sic_battery_t bat = {0};
int rc = sic_battery_read(&bat);
/* bat.voltage_v — float, e.g. 3.85 */
/* bat.percent   — 0..100, or -1 if unavailable */
```

---

## I2C Bus

```c
#include <sic/bus/i2c_bus.h>

int sic_i2c_write    (int bus, uint8_t addr, const uint8_t* data, int len);
int sic_i2c_read     (int bus, uint8_t addr, uint8_t* data, int len);
int sic_i2c_writeread(int bus, uint8_t addr,
                      const uint8_t* wr, int wlen,
                      uint8_t* rd, int rlen);
```

Returns bytes transferred on success, negative on error.

---

## GPIO Bus

```c
#include <sic/bus/gpio_bus.h>

void sic_gpio_mode       (int pin, int output);   /* 1=output, 0=input */
void sic_gpio_mode_pullup(int pin);
void sic_gpio_write      (int pin, int val);
int  sic_gpio_read       (int pin);
```

---

## Delay

```c
void sic_delay_ms(int ms);
```

For direct I2S amp paths such as Cardputer/NS4168, the amp driver may also
provide portable playback hooks:

```c
int rc = amp->v->play_mono(amp, pcm16, frames, 16000);
int rc = amp->v->beep_ms(amp, 2000);
```

Prefer these hooks from examples/apps because they keep board speaker pins in
the SIC board backend instead of leaking them into application code.

---

## IR TX

```c
#include <sic/sic.h>

int rc = sic_ir_send_nec(0x00FF00FFu); /* 0 = sent */
```

Boards expose `SIC_CAP_IR_TX` only when a real IR driver is registered.
Cardputer uses GPIO44 with a 38 kHz NEC bitbang driver.

---

## SD Storage

```c
#include <sic/storage/sd.h>

const sd_t* sd = sic_sd(0);
int present = sd && sd->v && sd->v->present(sd);
uint64_t bytes = sd->v->card_size_bytes(sd);
```

`SIC_CAP_SD` means the selected board has an SD backend. `present()` means an SD
card is inserted and the backend could mount/probe it.
