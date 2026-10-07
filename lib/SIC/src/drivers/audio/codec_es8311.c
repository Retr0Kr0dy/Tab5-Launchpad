/*
 * codec_es8311.c — ES8311 audio codec driver for SIC.
 *
 * Platform-agnostic: uses only SIC HAL functions (sic_i2c_write,
 * sic_codec_open/read/write/close, sic_delay_ms).
 *
 * SIC supports two ES8311 clock topologies:
 *   1. external MCLK pin (T-Pager) — keep the proven 256×Fs init path;
 *   2. MCLK derived from BCLK/SCLK (Cardputer-ADV) — reprogram ES8311 when
 *      switching between ADC and DAC modes, following M5's ES8311 practice.
 *
 * The application/example never sees this.  Board cfg selects the topology.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sic/sic.h"
#include "sic/audio/mic.h"
#include "sic/audio/amp.h"
#include "sic/audio/codec_es8311.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/delay.h"

#define ES8311_REG_DAC_CTRL 0x31u
#define ES8311_DACMUTE_BITS 0x60u  /* bits 6+5: digital + analog mute */

typedef enum {
    ES_MODE_NONE = 0,
    ES_MODE_MIC  = 1,
    ES_MODE_AMP  = 2,
} es_mode_t;

/* ── External-MCLK init, used by T-Pager ──────────────────────────────── */

static const unsigned char k_init_mclk[][2] = {
    {0x44, 0x08},  /* GPIO1 = nSPKEN function, initial config         */
    {0x01, 0x30},  /* SYSCLK: ext MCLK in, MCLK enabled, pre-div /1  */
    {0x02, 0x00},  /* MCLK pre-divider /1                             */
    {0x03, 0x10},  /* BCLK: output mode                               */
    {0x16, 0x24},  /* MIC PGA gain 18 dB                              */
    {0x04, 0x01},  /* LRCK H = 0x01  → total div = 0x100 = 256       */
    {0x05, 0x00},  /* LRCK L = 0x00                                   */
    {0x0B, 0x00},
    {0x0C, 0x00},
    {0x10, 0x1F},  /* analog: inputs disabled during boot             */
    {0x11, 0x7F},
    {0x00, 0x80},  /* soft power-up                                   */
    {0x01, 0x3F},  /* SYSCLK: ext MCLK, slow-clk off, slave mode      */
    {0x13, 0x10},
    {0x1B, 0x0A},
    {0x1C, 0x6A},
    {0x44, 0x08},
    {0x17, 0xBF},  /* ADC digital volume = 0 dB-ish                   */
    {0x0E, 0x02},  /* MIC bias/reference on                            */
    {0x12, 0x00},  /* DAC analog power on                              */
    {0x14, 0x1A},  /* analog mic path                                  */
    {0x0D, 0x01},  /* VMID normal                                      */
    {0x15, 0x40},
    {0x31, 0x00},  /* DAC unmute                                      */
    {0x32, 0xBF},  /* DAC volume                                      */
    {0x37, 0x08},  /* EQ bypass                                       */
    {0x45, 0x00},
};

/* ── BCLK-derived-MCLK init, used by Cardputer-ADV ────────────────────── */

/*
 * These short sequences intentionally mirror M5's ES8311 handling for boards
 * without a routed MCLK pin: REG01 bit7 selects BCLK/SCLK as ES8311's clock
 * source, REG02=0x18 applies the multiplier/pre-divider combination used by
 * M5.  ADC and DAC modes are written separately because several ES8311 paths
 * are power-gated by mode-specific registers.
 */
static const unsigned char k_init_bclk_mic[][2] = {
    {0x00, 0x80},  /* CSM power on / soft reset-ish                   */
    {0x01, 0xBA},  /* MCLK source = BCLK/SCLK                          */
    {0x02, 0x18},  /* MULT_PRE=3                                      */
    {0x0D, 0x01},  /* power up analog circuitry                        */
    {0x0E, 0x02},  /* MIC bias/reference on                            */
    {0x12, 0x00},  /* keep DAC powered enough for stable bias          */
    {0x14, 0x10},  /* Mic1P/Mic1N analog input, PGA minimum baseline   */
    {0x16, 0x24},  /* MIC PGA gain 18 dB                               */
    {0x17, 0xFF},  /* ADC digital volume high                          */
    {0x1C, 0x6A},  /* ADC automatic level / HPF config                 */
    {0x31, ES8311_DACMUTE_BITS},
    {0x44, 0x00},  /* no DAC→ADC feedback / GPIO side effects          */
    {0x45, 0x00},
};

static const unsigned char k_init_bclk_amp[][2] = {
    {0x00, 0x80},  /* CSM power on                                     */
    {0x01, 0xB5},  /* MCLK source = BCLK/SCLK                          */
    {0x02, 0x18},  /* MULT_PRE=3                                      */
    {0x0D, 0x01},  /* power up analog circuitry                        */
    {0x12, 0x00},  /* power-up DAC                                     */
    {0x13, 0x10},  /* enable output to HP/speaker drive                */
    {0x31, 0x00},  /* DAC unmute                                      */
    {0x32, 0xFF},  /* loud enough for tiny speaker during diagnostics  */
    {0x37, 0x08},  /* EQ bypass                                       */
    {0x44, 0x08},  /* GPIO1/nSPKEN function for speaker amp gate       */
    {0x45, 0x00},
};

/* ── Shared singleton ──────────────────────────────────────────────────── */

typedef struct {
    int           i2c_bus;
    unsigned char i2c_addr;
    int           pin_mclk, pin_bclk, pin_ws, pin_dout, pin_din;
    int           sample_rate;
    int           initialized;       /* 0=pending  1=ok */
    int           i2s_open;
    int           clock_from_bclk;
    es_mode_t     mode;
} es8311_ctx_t;

static es8311_ctx_t* g_es = NULL;

static int es_i2c_write(es8311_ctx_t* e, unsigned char reg, unsigned char val) {
    unsigned char buf[2] = { reg, val };
    return sic_i2c_write(e->i2c_bus, e->i2c_addr, buf, 2) < 0 ? -1 : 0;
}

static int es_write_seq(es8311_ctx_t* e, const unsigned char (*seq)[2], unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        if (es_i2c_write(e, seq[i][0], seq[i][1]) != 0) return -1;
        sic_delay_ms(2);
    }
    return 0;
}

static int es_chip_configure(es8311_ctx_t* e, es_mode_t mode) {
    if (!e) return -1;
    if (mode == ES_MODE_NONE) mode = ES_MODE_AMP;

    if (!e->clock_from_bclk) {
        if (e->initialized == 1) return 0;
        if (es_write_seq(e, k_init_mclk, (unsigned)(sizeof(k_init_mclk) / sizeof(k_init_mclk[0]))) != 0) {
            e->initialized = 0;
            return -1;
        }
        e->initialized = 1;
        e->mode = mode;
        return 0;
    }

    /* BCLK-derived topology needs mode-specific programming. */
    if (e->initialized == 1 && e->mode == mode) return 0;
    const unsigned char (*seq)[2] = NULL;
    unsigned count = 0;
    if (mode == ES_MODE_MIC) {
        seq = k_init_bclk_mic;
        count = (unsigned)(sizeof(k_init_bclk_mic) / sizeof(k_init_bclk_mic[0]));
    } else {
        seq = k_init_bclk_amp;
        count = (unsigned)(sizeof(k_init_bclk_amp) / sizeof(k_init_bclk_amp[0]));
    }
    if (es_write_seq(e, seq, count) != 0) {
        e->initialized = 0;
        e->mode = ES_MODE_NONE;
        return -1;
    }
    e->initialized = 1;
    e->mode = mode;
    return 0;
}

static es8311_ctx_t* get_or_create(const sic_es8311_cfg_t* cfg) {
    if (g_es) return g_es;
    g_es = (es8311_ctx_t*)calloc(1, sizeof *g_es);
    if (!g_es) return NULL;
    g_es->i2c_bus     = cfg->i2c_bus;
    g_es->i2c_addr    = cfg->i2c_addr;
    g_es->pin_mclk    = cfg->pin_mclk;
    g_es->pin_bclk    = cfg->pin_bclk;
    g_es->pin_ws      = cfg->pin_ws;
    g_es->pin_dout    = cfg->pin_dout;
    g_es->pin_din     = cfg->pin_din;
    g_es->sample_rate = 16000;
    g_es->clock_from_bclk = (cfg->clock_from_bclk || cfg->pin_mclk < 0) ? 1 : 0;
    g_es->mode = ES_MODE_NONE;
    return g_es;
}

static int es_prepare(es8311_ctx_t* e, int hz, es_mode_t mode) {
    if (!e) return -1;
    if (hz <= 0) hz = 16000;

    if (!e->i2s_open || e->sample_rate != hz) {
        if (e->i2s_open) { sic_codec_close(); e->i2s_open = 0; }
        e->sample_rate = hz;
        e->initialized = 0;
        e->mode = ES_MODE_NONE;
        int r = sic_codec_open(e->pin_mclk, e->pin_bclk, e->pin_ws,
                               e->pin_dout, e->pin_din, hz);
        if (r != 0) return -1;
        e->i2s_open = 1;
        sic_delay_ms(e->clock_from_bclk ? 25 : 15);
    }
    return es_chip_configure(e, mode);
}

static void es_close_stream(es8311_ctx_t* e) {
    if (!e || !e->i2s_open) return;
    sic_codec_close();
    e->i2s_open = 0;
    /* In legacy I2S, closing the port stops BCLK/MCLK; force a reconfigure on
     * the next start so the ES8311 never keeps a stale ADC/DAC power state.
     */
    e->initialized = 0;
    e->mode = ES_MODE_NONE;
}

static int es_set_dac_mute(es8311_ctx_t* e, int mute) {
    if (!e || es_chip_configure(e, ES_MODE_AMP) != 0) return -1;
    return es_i2c_write(e, ES8311_REG_DAC_CTRL, mute ? ES8311_DACMUTE_BITS : 0x00u);
}

/* ── mic_t vtable ──────────────────────────────────────────────────────── */

static int es_mic_start(const void* self, int hz) {
    es8311_ctx_t* e = (es8311_ctx_t*)((const mic_t*)self)->impl;
    return es_prepare(e, hz, ES_MODE_MIC);
}

static int es_mic_read(const void* self, short* buf, int n) {
    es8311_ctx_t* e = (es8311_ctx_t*)((const mic_t*)self)->impl;
    if (!e || !e->i2s_open || e->initialized != 1 || e->mode != ES_MODE_MIC) return 0;
    return sic_codec_read(buf, n, 200);
}

static const struct mic_vtbl_s MIC_VT = { es_mic_start, es_mic_read };

/* ── amp_t vtable ──────────────────────────────────────────────────────── */

static void es_amp_enable(const void* self, int on) {
    es8311_ctx_t* e = (es8311_ctx_t*)((const amp_t*)self)->impl;
    if (es_prepare(e, e ? e->sample_rate : 16000, ES_MODE_AMP) != 0) return;
    es_set_dac_mute(e, on ? 0 : 1);
}

static void es_write_silence(int sample_rate_hz) {
    short zero[256];
    memset(zero, 0, sizeof(zero));
    int frames = sample_rate_hz / 10; /* 100 ms */
    while (frames > 0) {
        int n = frames > 256 ? 256 : frames;
        sic_codec_write(zero, n);
        frames -= n;
    }
}

static int es_amp_play_mono(const void* self, const int16_t* mono, size_t nframes, int sample_rate_hz) {
    es8311_ctx_t* e = (es8311_ctx_t*)((const amp_t*)self)->impl;
    if (!e || !mono || nframes == 0) return -1;
    if (sample_rate_hz <= 0) sample_rate_hz = 16000;
    if (es_prepare(e, sample_rate_hz, ES_MODE_AMP) != 0) return -1;
    if (es_set_dac_mute(e, 0) != 0) return -2;

    int written = 0;
    const int16_t* p = mono;
    size_t left = nframes;
    while (left) {
        int chunk = left > 512 ? 512 : (int)left;
        int n = sic_codec_write(p, chunk);
        if (n <= 0) break;
        written += n;
        p += n;
        left -= (size_t)n;
    }

    es_write_silence(sample_rate_hz);
    es_set_dac_mute(e, 1);
    es_close_stream(e);
    return written;
}

static int es_amp_beep_ms(const void* self, unsigned ms) {
    es8311_ctx_t* e = (es8311_ctx_t*)((const amp_t*)self)->impl;
    if (!e) return -1;
    if (ms == 0) ms = 1;
    const int sr = 16000;
    if (es_prepare(e, sr, ES_MODE_AMP) != 0) return -1;
    if (es_set_dac_mute(e, 0) != 0) return -2;

    enum { CHUNK = 256 };
    int16_t buf[CHUNK];
    uint32_t total = (uint32_t)((uint64_t)sr * ms / 1000u);
    uint32_t phase = 0;
    while (total) {
        int n = total > CHUNK ? CHUNK : (int)total;
        for (int i = 0; i < n; ++i) {
            /* 1 kHz square wave at 16 kHz: 8 samples high, 8 low. */
            buf[i] = ((phase++ / 8u) & 1u) ? 14000 : -14000;
        }
        sic_codec_write(buf, n);
        total -= (uint32_t)n;
    }

    es_write_silence(sr);
    es_set_dac_mute(e, 1);
    es_close_stream(e);
    return 0;
}

static const struct amp_vtbl_s AMP_VT = {
    .enable = es_amp_enable,
    .play_mono = es_amp_play_mono,
    .beep_ms = es_amp_beep_ms,
};

/* ── Probe — always succeeds (no I2C / I2S at probe time) ──────────────── */

static mic_t g_mic_inst;
static amp_t g_amp_inst;

static int probe_mic(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "codec_es8311") != 0) return -1;
    es8311_ctx_t* ctx = get_or_create((const sic_es8311_cfg_t*)d->cfg);
    if (!ctx) return -1;
    g_mic_inst.v    = &MIC_VT;
    g_mic_inst.impl = ctx;
    *out = &g_mic_inst;
    return 0;
}

static int probe_amp(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "codec_es8311") != 0) return -1;
    es8311_ctx_t* ctx = get_or_create((const sic_es8311_cfg_t*)d->cfg);
    if (!ctx) return -1;
    g_amp_inst.v    = &AMP_VT;
    g_amp_inst.impl = ctx;
    *out = &g_amp_inst;
    return 0;
}

/* ── Driver registration ───────────────────────────────────────────────── */

static const sic_driver_t DRV_MIC = {
    "codec_es8311", SIC_F_MIC, probe_mic, NULL
};
static const sic_driver_t DRV_AMP = {
    "codec_es8311", SIC_F_AMP, probe_amp, NULL
};

void sic_register_driver_codec_es8311(void) {
    sic_registry_register(&DRV_MIC);
    sic_registry_register(&DRV_AMP);
}
