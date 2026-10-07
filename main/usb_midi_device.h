/* USB-MIDI device transport. The controller role is device-only.
 * M5Stack documents Type-A as Host and Type-C as USB 2.0 OTG.
 * Actual TinyUSB port/PHY routing must be checked against the hardware
 * revision and IDF configuration; do not infer it from a connector name.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Installs the TinyUSB MIDI device stack. Call once, from app_main(),
 * after display/SD bring-up. Idempotent. */
esp_err_t orion_usb_midi_init(void);

/* True once orion_usb_midi_init() has succeeded. */
bool orion_usb_midi_ready(void);

/* True after host enumeration. This does NOT prove that a DAW has opened
 * the port, accepted a message or configured a mapping. */
bool orion_usb_midi_host_connected(void);

/*
 * packet3 is a raw 3-byte MIDI message (status byte, data1, data2) --
 * cable number 0 is assumed. Non-blocking. Returns 0 on success, <0 if
 * not ready, no host mounted, or the TX FIFO is full.
 */
int orion_usb_midi_send(const uint8_t packet3[3]);

/* 0 if not ready. Otherwise mirrors tud_midi_available()'s own return. */
int orion_usb_midi_available(void);

/* Reads one complete incoming MIDI message into out3 (status, data1,
 * data2), discarding the USB-MIDI packet's own cable/CIN header byte.
 * Returns 0 on success, <0 if not ready or nothing is available. */
int orion_usb_midi_recv(uint8_t out3[3]);

#ifdef __cplusplus
}
#endif
