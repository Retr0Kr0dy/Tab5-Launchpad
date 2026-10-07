/*
 * posix_stub_backend.c — TEST-ONLY stub backend for the native link-check
 * harness (tests/native/test_tab5_link.c). NOT a real SIC_BACKEND_POSIX
 * implementation (sic/sic_backend.h documents that backend as "expected in
 * src/backends/posix/", which does not exist yet). This file exists solely
 * so board_tab5.c and every pure-C99 Tab5 driver can be linked into one
 * binary on a development machine, with every I2C/GPIO/delay/audio call
 * resolving to a harmless no-op instead of a real peripheral. It proves the
 * *symbol graph* resolves end-to-end; it says nothing about real hardware
 * behavior.
 *
 * All I2C reads report "nothing present" (NACK-equivalent, i.e. every
 * probe/scan returns 0 devices, every read returns -1) so board preinit()
 * and every driver's lazy-init path exercise their real failure paths
 * rather than fabricating plausible-looking sensor data.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sic/bus/gpio_bus.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/delay.h"
#include "sic/hal.h"
#include "sic/audio.h"

void sic_gpio_mode(int pin, int output) { (void)pin; (void)output; }
void sic_gpio_write(int pin, int val) { (void)pin; (void)val; }
int  sic_gpio_read(int pin) { (void)pin; return 0; }
void sic_gpio_mode_pullup(int pin) { (void)pin; }
void sic_gpio_mode_pulldown(int pin) { (void)pin; }

void sic_delay_ms(uint32_t ms) { (void)ms; }
unsigned long sic_millis(void) { return 0; }

int sic_i2c_begin_bus(int bus, int sda, int scl, uint32_t hz) {
    (void)bus; (void)sda; (void)scl; (void)hz;
    return 0;
}
int sic_i2c_scan_bus(int bus, uint8_t* addrs, int max) {
    (void)bus; (void)addrs; (void)max;
    return 0; /* nothing on the bus in this stub */
}
int sic_i2c_write(int bus, uint8_t addr, const uint8_t* buf, int n) {
    (void)bus; (void)addr; (void)buf;
    return n; /* pretend every write lands, so init sequences "succeed" */
}
int sic_i2c_read(int bus, uint8_t addr, uint8_t* buf, int n) {
    (void)bus; (void)addr;
    if (buf && n > 0) memset(buf, 0, (size_t)n);
    return -1; /* no real device answers in this stub */
}
int sic_i2c_writeread(int bus, uint8_t addr, const uint8_t* wr, int nw, uint8_t* rd, int nr) {
    (void)bus; (void)addr; (void)wr; (void)nw;
    if (rd && nr > 0) memset(rd, 0, (size_t)nr);
    return -1;
}

int sic_sysinfo(sic_sysinfo_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->chip_model = "native-link-check-stub";
    return 0;
}

/* Audio: SIC's contract (sic/audio.h) has no POSIX implementation yet
 * either (same "expected in src/backends/posix/, not built" situation as
 * gpio/i2c/delay above). Stub every entry point as a harmless no-op so
 * codec_es8388.c / codec_es7210.c link. */
int sic_spk_open(int bclk_pin, int ws_pin, int dout_pin, int sample_rate_hz) {
    (void)bclk_pin; (void)ws_pin; (void)dout_pin; (void)sample_rate_hz;
    return 0;
}
int sic_spk_write(const int16_t* mono, size_t nframes) { (void)mono; return (int)nframes; }
void sic_spk_close(void) {}
int sic_spk_beep_1khz_ms(unsigned ms) { (void)ms; return 0; }

int sic_mic_open(int clk_pin, int data_pin, int sample_rate_hz, int right_slot) {
    (void)clk_pin; (void)data_pin; (void)sample_rate_hz; (void)right_slot;
    return 0;
}
int sic_mic_read(int16_t* out_frames, size_t max_frames, int timeout_ms) {
    (void)timeout_ms;
    if (out_frames && max_frames) memset(out_frames, 0, max_frames * sizeof(int16_t));
    return (int)max_frames;
}
void sic_mic_close(void) {}

int sic_codec_open(int mclk_pin, int bclk_pin, int ws_pin, int dout_pin, int din_pin, int sample_rate_hz) {
    (void)mclk_pin; (void)bclk_pin; (void)ws_pin; (void)dout_pin; (void)din_pin; (void)sample_rate_hz;
    return 0;
}
int sic_codec_read(int16_t* buf, int n, int timeout_ms) {
    (void)timeout_ms;
    if (buf && n > 0) memset(buf, 0, (size_t)n * sizeof(int16_t));
    return n;
}
int sic_codec_write(const int16_t* mono, int n) { (void)mono; return n; }
void sic_codec_close(void) {}
void sic_codec_flush(void) {}

int sic_codec_mic4_open(int mclk_pin, int bclk_pin, int ws_pin, int din_pin, int sample_rate_hz) {
    (void)mclk_pin; (void)bclk_pin; (void)ws_pin; (void)din_pin; (void)sample_rate_hz;
    return 0;
}
int sic_codec_read4(int16_t* buf, int n, int timeout_ms) {
    (void)timeout_ms;
    if (buf && n > 0) memset(buf, 0, (size_t)n * sizeof(int16_t));
    return n;
}
