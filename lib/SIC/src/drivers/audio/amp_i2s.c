#define SIC_AUDIO_NO_COMPAT 1
/*
 * amp_i2s.c — typed SIC amp_t wrapper for direct I2S speaker amplifier paths.
 *
 * The first supported target is Cardputer's NS4168.  There is no separate amp
 * enable GPIO in the public pin map, so enable() is a no-op except for keeping
 * a typed capability in the registry.  Actual audio output is through
 * sic_spk_open/write/close and helpers such as sic_spk_beep_1khz_ms().
 */

#include <string.h>
#include "sic/sic.h"
#include "sic/audio.h"
#include "sic/audio/amp.h"
#include "sic/audio/i2s_amp.h"
#include "sic/sic_registry.h"

typedef struct {
    sic_i2s_amp_cfg_t cfg;
    int enabled;
} i2s_amp_ctx_t;

static i2s_amp_ctx_t g_ctx;
static amp_t g_amp;

static void i2s_amp_enable(const void* self, int on) {
    i2s_amp_ctx_t* c = (i2s_amp_ctx_t*)((const amp_t*)self)->impl;
    if (c) c->enabled = on ? 1 : 0;
}

static int i2s_amp_play_mono(const void* self, const int16_t* mono, size_t nframes, int sample_rate_hz) {
    i2s_amp_ctx_t* c = (i2s_amp_ctx_t*)((const amp_t*)self)->impl;
    if (!c || !mono || nframes == 0) return -1;
    if (sample_rate_hz <= 0) sample_rate_hz = 16000;

    int rc = sic_spk_open(c->cfg.bclk_pin, c->cfg.ws_pin, c->cfg.dout_pin, sample_rate_hz);
    if (rc != 0) return rc;

    int written = sic_spk_write(mono, nframes);

    /* Push a short zero tail, then close the I2S path.  This avoids replaying
     * stale DMA/shift-register content on simple amp paths.
     */
    int16_t silence[256];
    memset(silence, 0, sizeof(silence));
    unsigned zero_frames = (unsigned)(sample_rate_hz / 20); /* 50 ms */
    while (zero_frames) {
        size_t n = zero_frames > 256 ? 256 : zero_frames;
        sic_spk_write(silence, n);
        zero_frames -= (unsigned)n;
    }
    sic_spk_close();
    return written;
}

static int i2s_amp_beep_ms(const void* self, unsigned ms) {
    i2s_amp_ctx_t* c = (i2s_amp_ctx_t*)((const amp_t*)self)->impl;
    if (!c) return -1;
    if (ms == 0) ms = 1;

    const int sr = 16000;
    enum { AMP_I2S_CHUNK = 256 };
    int16_t buf[AMP_I2S_CHUNK];
    int rc = sic_spk_open(c->cfg.bclk_pin, c->cfg.ws_pin, c->cfg.dout_pin, sr);
    if (rc != 0) return rc;

    uint32_t total = (uint32_t)((uint64_t)sr * ms / 1000u);
    uint32_t phase = 0;
    while (total) {
        size_t n = total > AMP_I2S_CHUNK ? AMP_I2S_CHUNK : total;
        for (size_t i = 0; i < n; ++i) {
            /* 1 kHz square wave: cheap, deterministic, no libm dependency in this driver. */
            buf[i] = ((phase++ / 8u) & 1u) ? 10000 : -10000;
        }
        sic_spk_write(buf, n);
        total -= (uint32_t)n;
    }

    memset(buf, 0, sizeof(buf));
    uint32_t zero_frames = (uint32_t)(sr / 20);
    while (zero_frames) {
        size_t n = zero_frames > AMP_I2S_CHUNK ? AMP_I2S_CHUNK : zero_frames;
        sic_spk_write(buf, n);
        zero_frames -= (uint32_t)n;
    }
    sic_spk_close();
    return 0;
}

static const struct amp_vtbl_s I2S_AMP_VT = {
    .enable = i2s_amp_enable,
    .play_mono = i2s_amp_play_mono,
    .beep_ms = i2s_amp_beep_ms,
};

static int probe_i2s_amp(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "ns4168") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_i2s_amp_cfg_t*)d->cfg;
    g_amp.v = &I2S_AMP_VT;
    g_amp.impl = &g_ctx;
    *out = &g_amp;
    return 0;
}

static const sic_driver_t DRV_I2S_AMP = {
    "ns4168", SIC_F_AMP, probe_i2s_amp, NULL
};

void sic_register_driver_amp_i2s(void) {
    sic_registry_register(&DRV_I2S_AMP);
}
