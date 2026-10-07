/*
 * codec_es8388.c — ES8388 audio codec driver for SIC (Tab5 speaker/DAC path).
 *
 * Platform-agnostic: uses only SIC HAL functions (sic_i2c_write,
 * sic_codec_open/write/close, sic_delay_ms) — same contract codec_es8311.c
 * uses, and the structural template for this file (single chip-config
 * singleton, probe() does zero I2C I/O, chip register init deferred to the
 * first amp.enable()/play_mono()/beep_ms() call).
 *
 * Registers only SIC_F_AMP (see amp_vtbl_s, include/sic/audio/amp.h): Tab5
 * only uses ES8388 for playback (speaker), never as a mic input — the mic
 * path on this board is a separate physical chip, ES7210 (codec_es7210.c).
 * ES8388 also has its own on-chip ADC, but it is left fully powered down /
 * unconfigured here since nothing on Tab5 wires it as a capture source.
 *
 * ── Shared I2S pins with ES7210, independent channels ──────────────────────
 * ES8388 and ES7210 sit on the same physical pins (Tab5: MCLK=30, BCLK=27,
 * WS=29 shared; DOUT=26 to ES8388, DIN=28 from ES7210), but they do not
 * share one duplex channel pair. This driver still calls the generic
 * sic_codec_open() (full TX+RX, RX simply unused since ES8388 is TX-only on
 * Tab5) -- real-hardware testing found that a genuine shared duplex pair
 * with ES7210's mismatched-slot-count TDM RX (2-slot STD TX vs 4-slot TDM
 * RX) reliably left RX timing out with zero bytes, so ES7210 now opens its
 * own independent RX-only channel via sic_codec_mic4_open() (sic/audio.h)
 * instead -- see that function's header comment for the full finding.
 * Since the two channels can't both electrically drive the shared pins at
 * once, sic_codec_open() and sic_codec_mic4_open() each tear the other
 * down before claiming the pins; Tab5 never needs simultaneous
 * record+playback, so this hand-off is a real fix rather than a
 * missing-feature workaround.
 *
 * ── Register map confidence ─────────────────────────────────────────────
 * No locally-vendored register-level source exists for this chip (the
 * reference firmware configures it through esp_codec_dev's
 * es8388_codec_new(), whose own register-level source is fetched by that
 * project's build, not vendored into the checked-out reference tree). ES8388
 * is nonetheless one of the most widely used I2S audio codecs in the ESP32
 * ecosystem (LyraT, many third-party dev boards) with dozens of independent
 * open-source drivers publicly cross-referencing the same register map:
 *   HIGH confidence (register *addresses*, cross-referenced across many
 *   independent public ES8388 drivers): 0x00/0x01 (CONTROL1/2, chip-level
 *   power+Vmid), 0x02 (CHIPPOWER), 0x03 (ADCPOWER), 0x04 (DACPOWER, output
 *   route enable to LOUT1/ROUT1/LOUT2/ROUT2), 0x08 (MASTERMODE, master/
 *   slave select), 0x17 (DACCONTROL1, serial format), 0x18 (DACCONTROL2,
 *   sample-rate divider), 0x19 (DACCONTROL3, DAC mute), 0x1A/0x1B
 *   (DACCONTROL4/5, L/R DAC digital volume), 0x26/0x27 (DACCONTROL16/17,
 *   mixer routing), 0x2D/0x2E (DACCONTROL23/24, LOUT2/ROUT2 analog volume).
 *   MEDIUM confidence (bit-level values within those registers — chosen to
 *   match the conventional "unity-ish gain, Philips I2S, slave mode, DAC
 *   powered to LOUT2/ROUT2" defaults seen across those same public drivers,
 *   but not independently re-verified against Everest Semi's datasheet in
 *   this environment): exact power-sequencing bit patterns in CONTROL1/2/
 *   CHIPPOWER/ADCPOWER, and the mixer-routing bits in DACCONTROL16/17.
 * Flagged per project convention (see touch_st712x.c / rtc_rx8130.c /
 * imu_bmi270.c for the same treatment) rather than presented as verified.
 *
 * Slave mode: the ESP32-P4 side (sic_codec_open(), Phase 2) always drives
 * I2S as I2S_ROLE_MASTER, so ES8388 must be configured as an I2S *slave*
 * taking external MCLK/BCLK/WS — mirrors codec_es8311.c's k_init_mclk path.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_*, sic_codec_*, sic_delay_ms from the SIC HAL, no Arduino/ESP-IDF
 * headers here.
 */
#include <stdint.h>
#include <string.h>
#include "sic/sic.h"
#include "sic/audio/amp.h"
#include "sic/audio/codec_es8388.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/delay.h"

/* ── Register addresses ───────────────────────────────────────────────────
 * Cross-checked against Espressif's own es8388.c/es8388_reg.h (component
 * espressif/esp_codec_dev, the same vendor driver this board's reference
 * firmware uses). Several addresses/values here were previously wrong
 * under a "medium confidence, not independently re-verified" caveat; this
 * replaces that guesswork with the real sequence. */
#define ES8388_REG_CONTROL1    0x00u
#define ES8388_REG_CONTROL2    0x01u
#define ES8388_REG_CHIPPOWER   0x02u
#define ES8388_REG_ADCPOWER    0x03u
#define ES8388_REG_DACPOWER    0x04u
#define ES8388_REG_MASTERMODE  0x08u
#define ES8388_REG_DACCTRL1    0x17u
#define ES8388_REG_DACCTRL2    0x18u
#define ES8388_REG_DACCTRL3    0x19u  /* bit 2 = DAC softmute */
#define ES8388_REG_DACCTRL4    0x1Au  /* left DAC digital volume  */
#define ES8388_REG_DACCTRL5    0x1Bu  /* right DAC digital volume */
#define ES8388_REG_DACCTRL16   0x26u  /* LIN/RIN vs LDAC/RDAC mixer source select */
#define ES8388_REG_DACCTRL17   0x27u  /* left DAC->left mixer enable/gain */
#define ES8388_REG_DACCTRL20   0x2Au  /* right DAC->right mixer enable/gain */
#define ES8388_REG_DACCTRL21   0x2Bu  /* DAC/ADC clock + "enable dac" bit -- real hardware needs this
                                        * written explicitly; the old init sequence never touched it. */
#define ES8388_REG_DACCTRL23   0x2Du  /* ramp-rate control ("vroi"), not a volume register */
#define ES8388_REG_DACCTRL24   0x2Eu  /* LOUT1 analog volume */
#define ES8388_REG_DACCTRL25   0x2Fu  /* ROUT1 analog volume */
#define ES8388_REG_DACCTRL26   0x30u  /* LOUT2 analog volume (Tab5's speaker route) */
#define ES8388_REG_DACCTRL27   0x31u  /* ROUT2 analog volume (Tab5's speaker route) */

#define ES8388_DACCTRL3_MUTE_BIT 0x04u /* bit 2: 1 = DAC softmute */

/*
 * Boot init sequence — a faithful 1:1 port of Espressif's own es8388_open()
 * (espressif/esp_codec_dev's device/es8388/es8388.c), same registers, same
 * values, same order, minus the ADC-path writes (ES8388_ADCCONTROL1-5):
 * Tab5 never uses this chip's ADC as a capture source (ES7210 owns mic
 * input — see the file header), so ADCPOWER is left at its 0xFF power-down
 * default instead of following the reference's ADC bring-up, same deliberate
 * scope decision the old sequence already made. Everything DAC/output-path
 * related now matches the reference exactly, including two real bugs the
 * old best-guess sequence had: CONTROL1 was 0x06 instead of the reference's
 * 0x12, and DACCONTROL21 (the actual "enable dac" register) was never
 * written at all.
 */
static const unsigned char k_init[][2] = {
    {ES8388_REG_DACCTRL3,   ES8388_DACCTRL3_MUTE_BIT}, /* mute first, before anything else powers up */
    {ES8388_REG_CONTROL2,   0x50u},
    {ES8388_REG_CHIPPOWER,  0x00u}, /* normal all, power up all */
    {0x35u,                 0xA0u}, /* internal-DLL tweak, reference writes these unconditionally */
    {0x37u,                 0xD0u},
    {0x39u,                 0xD0u},
    {ES8388_REG_MASTERMODE, 0x00u}, /* slave mode: BCLK/WS/MCLK driven externally by the ESP32-P4 I2S master */
    {ES8388_REG_DACPOWER,   0xC0u}, /* disable DAC + Lout/Rout while configuring */
    {ES8388_REG_CONTROL1,   0x12u}, /* Enfr=0, Play&Record mode */
    {ES8388_REG_DACCTRL1,   0x18u}, /* Philips I2S, 16-bit words, matching Phase 2's I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG */
    {ES8388_REG_DACCTRL2,   0x02u}, /* DACFsMode single-speed, DACFsRatio 256 */
    {ES8388_REG_DACCTRL16,  0x00u}, /* audio on LIN1/RIN1 path (not the LIN2/RIN2 bypass alternative) */
    {ES8388_REG_DACCTRL17,  0x90u}, /* left DAC -> left mixer enabled, 0dB */
    {ES8388_REG_DACCTRL20,  0x90u}, /* right DAC -> right mixer enabled, 0dB */
    {ES8388_REG_DACCTRL21,  0x80u}, /* enable dac -- the write the old sequence was missing entirely */
    {ES8388_REG_DACCTRL23,  0x00u}, /* vroi=0 */
    {ES8388_REG_DACCTRL4,   0x00u}, /* left DAC digital volume: 0dB */
    {ES8388_REG_DACCTRL5,   0x00u}, /* right DAC digital volume: 0dB */
    {ES8388_REG_DACCTRL24,  0x1Eu}, /* LOUT1 analog volume: 0dB */
    {ES8388_REG_DACCTRL25,  0x1Eu}, /* ROUT1 analog volume: 0dB */
    {ES8388_REG_DACCTRL26,  0x00u}, /* LOUT2 analog volume: 0dB (this register's own 0=unity scale, not -30dB like DACCTRL24/25) */
    {ES8388_REG_DACCTRL27,  0x00u}, /* ROUT2 analog volume: 0dB, symmetric with DACCTRL26 */
    {ES8388_REG_DACPOWER,   0x3Cu}, /* power up DAC + enable LOUT1/LOUT2/ROUT1/ROUT2 output drivers */
};

typedef struct {
    int           i2c_bus;
    unsigned char i2c_addr;
    int           pin_mclk, pin_bclk, pin_ws, pin_dout, pin_din;
    int           sample_rate;
    int           initialized;  /* 0=pending  1=ok */
    int           i2s_open;
    int           volume_pct;  /* 0-100, applied to DACCTRL4/5 on every set_volume() call */
} es8388_ctx_t;

static es8388_ctx_t g_es;
static amp_t         g_amp_inst;

static int es_i2c_write(es8388_ctx_t* e, unsigned char reg, unsigned char val) {
    unsigned char buf[2] = { reg, val };
    return sic_i2c_write(e->i2c_bus, e->i2c_addr, buf, 2) < 0 ? -1 : 0;
}

static int es_write_seq(es8388_ctx_t* e, const unsigned char (*seq)[2], unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        if (es_i2c_write(e, seq[i][0], seq[i][1]) != 0) return -1;
        sic_delay_ms(2);
    }
    return 0;
}

static void es_set_dac_volume_reg(es8388_ctx_t* e, int percent);

/* k_init hardcodes DACCTRL4/5 (the digital-volume registers) back to 0x00
 * (0dB / 100%) as part of the full init sequence -- so any previously-set
 * volume_pct must be re-applied here, or it would silently discard on
 * every re-init (es_prepare() resets `initialized` whenever the sample
 * rate changes or i2s wasn't already open, i.e. on essentially every
 * app's own audio_stream_open() after a different app or an idle period). */
static int es_chip_configure(es8388_ctx_t* e) {
    if (!e) return -1;
    if (e->initialized == 1) return 0;
    if (es_write_seq(e, k_init, (unsigned)(sizeof(k_init) / sizeof(k_init[0]))) != 0) {
        e->initialized = 0;
        return -1;
    }
    e->initialized = 1;
    es_set_dac_volume_reg(e, e->volume_pct);
    return 0;
}

static int es_prepare(es8388_ctx_t* e, int hz) {
    if (!e) return -1;
    if (hz <= 0) hz = 16000;

    if (!e->i2s_open || e->sample_rate != hz) {
        e->sample_rate = hz;
        e->initialized = 0; /* shared bus may have been retuned by ES7210 too */
        int r = sic_codec_open(e->pin_mclk, e->pin_bclk, e->pin_ws,
                               e->pin_dout, e->pin_din, hz);
        if (r != 0) return -1;
        e->i2s_open = 1;
        sic_delay_ms(15); /* MCLK-before-I2C settle, per DESIGN_INVARIANTS.md */
    }
    return es_chip_configure(e);
}

static int es_set_dac_mute(es8388_ctx_t* e, int mute) {
    if (!e || es_chip_configure(e) != 0) return -1;
    return es_i2c_write(e, ES8388_REG_DACCTRL3, mute ? ES8388_DACCTRL3_MUTE_BIT : 0x00u);
}

/*
 * DAC digital-volume write, registers per the reference firmware
 * (espressif/esp_codec_dev's device/es8388/es8388.c: es8388_set_vol()
 * writes DACCONTROL5 then DACCONTROL4, one register LSB = 0.5dB, 0xC0 =
 * -96dB floor).
 *
 * Percent maps onto a 40dB span, not the chip's full 96dB range: human
 * loudness perception is logarithmic and roughly obeys "-10dB = half as
 * loud", so spreading percent linearly across the full 96dB span makes
 * mid-range settings (50%, 75%) sound much quieter than their percentage
 * suggests. Since dB is already a perceptual unit, a straight
 * linear-percent-to-dB mapping is the right shape; only the span needs to
 * be tighter. Rescaled to -10dB per 25%: 100%=0dB, 75%=-10dB (perceptually
 * about half), 50%=-20dB, 25%=-30dB, 0%=-40dB (quiet but not the full
 * theoretical -96dB floor -- nothing in this project needs true bit-level
 * silence from this knob). System volume in Settings > About caps whatever
 * any app's own volume already computed (see orion_system_volume_set()'s
 * own comment, orion.h) -- if this curve is ever tuned again, keep an eye
 * on that use case's own expectations too.
 */
static void es_set_dac_volume_reg(es8388_ctx_t* e, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    /* 0..100 -> 0.0 .. -40.0 dB, then dB -> register at 0.5dB/LSB. */
    int reg = (100 - percent) * 80 / 100;
    if (reg < 0) reg = 0;
    if (reg > 80) reg = 80;
    es_i2c_write(e, ES8388_REG_DACCTRL5, (unsigned char)reg);
    es_i2c_write(e, ES8388_REG_DACCTRL4, (unsigned char)reg);
}

static void es_amp_set_volume(const void* self, int percent) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    if (!e || es_chip_configure(e) != 0) return;
    e->volume_pct = (percent < 0) ? 0 : (percent > 100 ? 100 : percent);
    es_set_dac_volume_reg(e, e->volume_pct);
}

static int es_amp_get_volume(const void* self) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    return e ? e->volume_pct : 100;
}

static void es_amp_flush(const void* self) {
    (void)self;
    sic_codec_flush();
}

static void es_amp_mute(const void* self) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    /* Deliberately NOT es_prepare() -- that can call sic_codec_open(),
     * touching shared I2S state a caller of mute() may be relying on being
     * safe to invoke concurrently with another (possibly stuck) task still
     * using the I2S channel. es_set_dac_mute() itself only does
     * es_chip_configure() (idempotent once initialized) + one I2C register
     * write -- see amp_vtbl_s.mute's doc comment for why that matters. */
    es_set_dac_mute(e, 1);
}

/* ── amp_t vtable ──────────────────────────────────────────────────────── */

static void es_amp_enable(const void* self, int on) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    if (es_prepare(e, e ? e->sample_rate : 16000) != 0) return;
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
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    if (!e || !mono || nframes == 0) return -1;
    if (sample_rate_hz <= 0) sample_rate_hz = 16000;
    if (es_prepare(e, sample_rate_hz) != 0) return -1;
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
    return written;
}

static int es_amp_beep_ms(const void* self, unsigned ms) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    if (!e) return -1;
    if (ms == 0) ms = 1;
    const int sr = 16000;
    if (es_prepare(e, sr) != 0) return -1;
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
    return 0;
}

static int es_amp_stream_open(const void* self, int sample_rate_hz) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    if (!e) return -1;
    if (sample_rate_hz <= 0) sample_rate_hz = 16000;
    if (es_prepare(e, sample_rate_hz) != 0) return -1;
    if (es_set_dac_mute(e, 0) != 0) return -2;
    return 0;
}

static int es_amp_stream_write(const void* self, const int16_t* mono, size_t nframes) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    if (!e || !mono || nframes == 0) return -1;

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
    return written;
}

static void es_amp_stream_close(const void* self) {
    es8388_ctx_t* e = (es8388_ctx_t*)((const amp_t*)self)->impl;
    if (!e) return;
    es_write_silence(e->sample_rate);
    es_set_dac_mute(e, 1);
}

static const struct amp_vtbl_s AMP_VT = {
    es_amp_enable, es_amp_play_mono, es_amp_beep_ms,
    es_amp_stream_open, es_amp_stream_write, es_amp_stream_close,
    es_amp_set_volume, es_amp_get_volume, es_amp_flush, es_amp_mute,
};

/* ── Probe — always succeeds (no I2C / I2S at probe time) ──────────────── */

static int probe_amp(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "codec_es8388") != 0 || !d->cfg) return -1;

    const sic_es8388_cfg_t* cfg = (const sic_es8388_cfg_t*)d->cfg;
    memset(&g_es, 0, sizeof(g_es));
    g_es.i2c_bus     = cfg->i2c_bus;
    g_es.i2c_addr    = cfg->i2c_addr;
    g_es.pin_mclk    = cfg->pin_mclk;
    g_es.pin_bclk    = cfg->pin_bclk;
    g_es.pin_ws      = cfg->pin_ws;
    g_es.pin_dout    = cfg->pin_dout;
    g_es.pin_din     = cfg->pin_din;
    g_es.sample_rate = 16000;
    g_es.volume_pct  = 100; /* unity -- matches k_init's DACCTRL4/5=0x00 (0dB) default */

    g_amp_inst.v    = &AMP_VT;
    g_amp_inst.impl = &g_es;
    *out = &g_amp_inst;
    return 0;
}

/* ── Driver registration ───────────────────────────────────────────────── */

static const sic_driver_t DRV_AMP = { "codec_es8388", SIC_F_AMP, probe_amp, NULL };

void sic_register_driver_codec_es8388(void) {
    sic_registry_register(&DRV_AMP);
}
