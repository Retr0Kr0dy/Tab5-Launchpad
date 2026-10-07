/*
 * codec_es7210.c — ES7210 4-channel mic ADC driver for SIC (Tab5 mic path).
 *
 * Platform-agnostic: uses only SIC HAL functions (sic_i2c_write,
 * sic_codec_open/read/close, sic_delay_ms) — same contract codec_es8311.c
 * uses, and the structural template for this file (single chip-config
 * singleton, probe() does zero I2C I/O, chip register init deferred to the
 * first mic.start() call).
 *
 * Registers only SIC_F_MIC (see mic_vtbl_s, include/sic/audio/mic.h): Tab5's
 * ES7210 is capture-only silicon (no DAC/speaker path), and the playback
 * side of this board is the separate ES8388 chip (codec_es8388.c).
 *
 * ── Shared I2S pins with ES8388, independent channels ──────────────────────
 * ES7210 and ES8388 sit on the same physical I2S0 peripheral pins (Tab5:
 * MCLK=30, BCLK=27, WS=29 shared; DIN=28 from ES7210, DOUT=26 to ES8388),
 * but — unlike codec_es8311.c-style boards — this driver does NOT share a
 * duplex channel pair with ES8388's sic_codec_open(). It calls its own
 * sic_codec_mic4_open() (SIC_TARGET_TAB5-only, sic/audio.h) instead, which
 * opens RX completely independently and hands off/reclaims the shared pins
 * from ES8388's TX. A genuine shared duplex pair with mismatched TX/RX slot
 * counts (2-slot STD vs 4-slot TDM) -- even matching the reference
 * firmware's own init sequence byte-for-byte -- left RX timing out with
 * zero bytes on every read on real hardware; only isolating RX into its
 * own channel fixed it.
 *
 * ── 4-channel TDM capture ───────────────────────────────────────────────
 * All 4 mic channels are activated (not just 2), matching this board's
 * reference firmware rather than the 2-channel default some other M5Stack
 * SDKs use. Both the analog front end (MIC1..MIC4 bias + PGA power) and the
 * digital capture path follow that.
 *
 * The generic I2S backend (sic_codec_open/sic_codec_read) is a deliberate
 * 1:1 port of the stereo/mono-only STD I2S contract with no TDM support,
 * leaving this driver to open its own dedicated TDM channel instead.
 * sic_codec_mic4_open() (see above) is exactly that -- 4-slot TDM,
 * mirroring the reference firmware's own TDM config (mclk_multiple=256,
 * bclk_div=8, all 4 slots), on its own independent handle rather than
 * sharing ES8388's. ES7210's own SDP_INTERFACE2 register (0x12) is set to
 * 0x02 below (was 0x00) to switch its serial output to matching 4-slot TDM
 * framing, matching espressif/esp_codec_dev's own es7210_mic_select(),
 * which writes exactly this value once 3+ mics are selected. Sample rate
 * is forced to 48000 Hz in es_mic_start() below regardless of what the
 * caller requests: 16kHz TDM was tried and reliably timed out with zero
 * bytes, consistent with the reference firmware's own code only ever
 * validating ES7210 TDM at 48kHz.
 *
 * Reads go through sic_codec_read4() (sic/audio.h), not sic_codec_read():
 * the latter still expects 2-slot STD framing and downmixes to mono, which
 * would corrupt a 4-slot TDM stream rather than just drop channels 3-4.
 * sic_codec_read4() returns raw interleaved MIC1..MIC4 int16 samples with no
 * downmix -- callers doing simple peak/VU detection (touchmenu.c, cmd_mic in
 * commands.c) are channel-agnostic and need no changes; a caller wanting
 * per-mic separation reads buf[4*i+0..3] per frame i.
 *
 * ── Register map confidence ─────────────────────────────────────────────
 * No locally-vendored register-level source exists for this chip (UserDemo
 * configures it through esp_codec_dev's es7210_codec_new(), whose own
 * register-level source is not vendored into the checked-out reference
 * tree). ES7210 is a commonly used 4-channel mic ADC in the ESP32 ecosystem
 * (ESP32-S3-Korvo and similar dev boards), with multiple independent public
 * drivers referencing a broadly consistent register map:
 *   MEDIUM confidence (register *addresses*): 0x00 (RESET), 0x01 (CLOCK
 *   power management), 0x06 (ADC1-4 POWER_DOWN bits), 0x08 (MODE_CFG,
 *   master/slave select), 0x11/0x12 (SDP_INTERFACE1/2, serial format +
 *   slot count), 0x40 (analog/mic-bias enable), 0x41-0x44 (per-channel MIC
 *   PGA gain).
 *   LOW confidence (exact bit-level values within those registers): chosen
 *   to match the conventional "slave mode, 2-slot/16-bit I2S, all 4 ADC
 *   channels analog-powered, mid PGA gain, mic bias on" shape described
 *   across those public references, NOT independently re-verified against
 *   Everest Semi's datasheet in this environment.
 * Flagged per project convention (see touch_st712x.c / rtc_rx8130.c /
 * imu_bmi270.c / codec_es8388.c for the same treatment) rather than
 * presented as verified — this file carries the lowest confidence of any
 * driver in the Tab5 bring-up so far and should be the first thing checked
 * against a bus trace on real hardware.
 *
 * Slave mode: the ESP32-P4 side (sic_codec_open(), Phase 2) always drives
 * I2S as I2S_ROLE_MASTER, so ES7210 must be configured as an I2S *slave*
 * taking external MCLK/BCLK/WS.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_*, sic_codec_*, sic_delay_ms from the SIC HAL, no Arduino/ESP-IDF
 * headers here.
 */
#include <stdint.h>
#include <string.h>
#include "sic/sic.h"
#include "sic/audio/mic.h"
#include "sic/audio/codec_es7210.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/delay.h"

/* ── Register addresses ───────────────────────────────────────────────────
 * Cross-checked against Espressif's own es7210.c/es7210_reg.h (component
 * espressif/esp_codec_dev, the same vendor driver this board's reference
 * firmware uses -- see codec_es8388.c's header for how it was obtained).
 * An earlier register map here had a real, serious bug: 0x41-0x44 were
 * labelled MIC1-4_GAIN, but on the real chip 0x41/0x42 are the MIC12/34
 * BIAS registers (not gain) and the actual per-mic gain registers are
 * 0x43-0x46 — every gain write below 0x44 was landing one register short of
 * where it was meant to, and the real per-mic POWER registers (0x47-0x4C)
 * were never written at all, leaving the mic PGAs powered down regardless
 * of POWER_DOWN_REG06's state.
 */
#define ES7210_REG_RESET        0x00u
#define ES7210_REG_CLOCK_OFF    0x01u  /* bitmask, 1 = that channel group's clock gated off */
#define ES7210_REG_MAINCLK      0x02u
#define ES7210_REG_OSR          0x07u
#define ES7210_REG_POWER_DOWN   0x06u  /* bits 0-3: ADC1..ADC4 power-down, active 1 */
#define ES7210_REG_MODE_CFG     0x08u  /* bit 0: 0=slave, 1=master */
#define ES7210_REG_TIME_CTRL0   0x09u  /* chip-state-transition timing */
#define ES7210_REG_TIME_CTRL1   0x0Au  /* power-on-state timing */
#define ES7210_REG_SDP_IF1      0x11u  /* serial format: word length / I2S vs left-justified */
#define ES7210_REG_SDP_IF2      0x12u  /* TDM slot-count select; 0 = 2-slot/standard I2S */
#define ES7210_REG_ADC34_HPF2   0x20u
#define ES7210_REG_ADC34_HPF1   0x21u
#define ES7210_REG_ADC12_HPF1   0x22u
#define ES7210_REG_ADC12_HPF2   0x23u
#define ES7210_REG_ANALOG       0x40u  /* analog power / VDDA / VMID select */
#define ES7210_REG_MIC12_BIAS   0x41u
#define ES7210_REG_MIC34_BIAS   0x42u
#define ES7210_REG_MIC1_GAIN    0x43u
#define ES7210_REG_MIC2_GAIN    0x44u
#define ES7210_REG_MIC3_GAIN    0x45u
#define ES7210_REG_MIC4_GAIN    0x46u
#define ES7210_REG_MIC1_POWER   0x47u
#define ES7210_REG_MIC2_POWER   0x48u
#define ES7210_REG_MIC3_POWER   0x49u
#define ES7210_REG_MIC4_POWER   0x4Au
#define ES7210_REG_MIC12_POWER  0x4Bu
#define ES7210_REG_MIC34_POWER  0x4Cu

/* Gain register low nibble is a 4-bit code, not raw dB; the reference's
 * get_db(30.0) resolves to code 0x0A (~30dB, its own open()-time default),
 * OR'd with bit 0x10 which the reference always sets alongside a selected
 * mic's gain. */
#define ES7210_MIC_GAIN_DEFAULT 0x1Au

/*
 * Boot init sequence — a faithful port of Espressif's es7210_open() followed
 * by the register writes es7210_start()/es7210_mic_select() make to select
 * all 4 mic inputs (matching UserDemo's MIC1|MIC2|MIC3|MIC4 intent — see the
 * 4-channel TDM section above for the corresponding digital-capture path). The reference does some of this as read-modify-
 * write against whatever state the chip is already in (`update_reg_bit`);
 * since this is a from-cold-reset bring-up where every prior register value
 * is already known (we just wrote it), the final settled values are used
 * directly as plain writes instead, which is equivalent and keeps this a
 * flat table like every other SIC codec driver.
 */
static const unsigned char k_init[][2] = {
    {ES7210_REG_RESET,      0xFFu}, /* full chip reset */
    {ES7210_REG_RESET,      0x41u}, /* release reset, keep analog/digital domains gated during config */
    {ES7210_REG_CLOCK_OFF,  0x3Fu}, /* gate all channel clocks while configuring */
    {ES7210_REG_TIME_CTRL0, 0x30u},
    {ES7210_REG_TIME_CTRL1, 0x30u},
    {ES7210_REG_ADC12_HPF2, 0x2Au},
    {ES7210_REG_ADC12_HPF1, 0x0Au},
    {ES7210_REG_ADC34_HPF2, 0x0Au},
    {ES7210_REG_ADC34_HPF1, 0x2Au},
    {ES7210_REG_MODE_CFG,   0x00u}, /* slave mode: BCLK/WS/MCLK driven externally by the ESP32-P4 I2S master */
    {ES7210_REG_ANALOG,     0x43u}, /* analog power on, VDDA=3.3V */
    {ES7210_REG_MIC12_BIAS, 0x70u}, /* mic bias 2.87V */
    {ES7210_REG_MIC34_BIAS, 0x70u},
    {ES7210_REG_OSR,        0x20u},
    {ES7210_REG_MAINCLK,    0xC1u}, /* set ADC clock division, clears an internal state per the reference's own comment */
    {ES7210_REG_SDP_IF1,    0x60u}, /* 16-bit width, I2S (Philips) serial format */
    {ES7210_REG_SDP_IF2,    0x02u}, /* 4-slot TDM output — matches sic_codec_open()'s SIC_TARGET_TAB5
                                      * RX TDM config (i2s_tdm.h) and the real reference's
                                      * es7210_mic_select()'s TDM branch (>=3 mics selected -> 0x02). */
    {ES7210_REG_MIC1_GAIN,  ES7210_MIC_GAIN_DEFAULT},
    {ES7210_REG_MIC2_GAIN,  ES7210_MIC_GAIN_DEFAULT},
    {ES7210_REG_MIC3_GAIN,  ES7210_MIC_GAIN_DEFAULT},
    {ES7210_REG_MIC4_GAIN,  ES7210_MIC_GAIN_DEFAULT},
    {ES7210_REG_MIC12_POWER,0xFFu}, /* mask off before the group power-up below, matches reference ordering */
    {ES7210_REG_MIC34_POWER,0xFFu},
    {ES7210_REG_POWER_DOWN, 0x00u}, /* ADC1..ADC4 all powered up (0 = not powered down) */
    {ES7210_REG_MIC1_POWER, 0x08u},
    {ES7210_REG_MIC2_POWER, 0x08u},
    {ES7210_REG_MIC3_POWER, 0x08u},
    {ES7210_REG_MIC4_POWER, 0x08u},
    {ES7210_REG_CLOCK_OFF,  0x20u}, /* un-gate all 4 channels' clocks (0x3F with mic1-4's bits cleared) */
    {ES7210_REG_MIC12_POWER,0x00u}, /* power mic1+mic2 PGAs on */
    {ES7210_REG_MIC34_POWER,0x00u}, /* power mic3+mic4 PGAs on */
    {ES7210_REG_RESET,      0x71u}, /* final state-machine kick, matches the reference's es7210_start() tail */
    {ES7210_REG_RESET,      0x41u},
};

typedef struct {
    int           i2c_bus;
    unsigned char i2c_addr;
    int           pin_mclk, pin_bclk, pin_ws, pin_dout, pin_din;
    int           sample_rate;
    int           initialized;  /* 0=pending  1=ok */
    int           i2s_open;
} es7210_ctx_t;

static es7210_ctx_t g_es;
static mic_t         g_mic_inst;

static int es_i2c_write(es7210_ctx_t* e, unsigned char reg, unsigned char val) {
    unsigned char buf[2] = { reg, val };
    return sic_i2c_write(e->i2c_bus, e->i2c_addr, buf, 2) < 0 ? -1 : 0;
}

static int es_write_seq(es7210_ctx_t* e, const unsigned char (*seq)[2], unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        if (es_i2c_write(e, seq[i][0], seq[i][1]) != 0) return -1;
        sic_delay_ms(2);
    }
    return 0;
}

static int es_chip_configure(es7210_ctx_t* e) {
    if (!e) return -1;
    if (e->initialized == 1) return 0;
    if (es_write_seq(e, k_init, (unsigned)(sizeof(k_init) / sizeof(k_init[0]))) != 0) {
        e->initialized = 0;
        return -1;
    }
    e->initialized = 1;
    return 0;
}

/* ── mic_t vtable ──────────────────────────────────────────────────────── */

static int es_mic_start(const void* self, int hz) {
    es7210_ctx_t* e = (es7210_ctx_t*)((const mic_t*)self)->impl;
    if (!e) return -1;
    if (hz <= 0) hz = 16000;
#if defined(SIC_TARGET_TAB5)
    /* The reference firmware only ever exercises ES7210 4-slot TDM at
     * 48000 Hz -- its 16000 Hz variant is commented out at that call site.
     * 16kHz TDM reads reliably timed out with zero bytes on real hardware,
     * so 48000 is forced here regardless of what the caller asks for: every
     * caller on this board wants working capture, not a specific rate, and
     * 48000 is the only proven one. The
     * MCLK:Fs ratio (256x, both here and on the STD/ES8388 side) stays fixed
     * across the rate change, so ES7210's own MAINCLK/OSR register values in
     * k_init below (ratio-based, not absolute-Hz) do not need to change. */
    hz = 48000;
#endif

    if (!e->i2s_open || e->sample_rate != hz) {
        e->sample_rate = hz;
        e->initialized = 0; /* shared bus may have been retuned/reclaimed by ES8388 too */
#if defined(SIC_TARGET_TAB5)
        /* Independent RX-only channel, not a shared duplex pair with
         * ES8388's TX -- see sic_codec_mic4_open()'s header comment
         * (sic/audio.h) for the real-hardware finding that made this
         * necessary. Physically shares MCLK/BCLK/WS with ES8388's TX;
         * sic_codec_mic4_open() itself hands off/reclaims those pins. */
        int r = sic_codec_mic4_open(e->pin_mclk, e->pin_bclk, e->pin_ws, e->pin_din, hz);
#else
        int r = sic_codec_open(e->pin_mclk, e->pin_bclk, e->pin_ws,
                               e->pin_dout, e->pin_din, hz);
#endif
        if (r != 0) return -1;
        e->i2s_open = 1;
        sic_delay_ms(15); /* MCLK-before-I2C settle, per DESIGN_INVARIANTS.md */
    }
    return es_chip_configure(e);
}

static int es_mic_read(const void* self, short* buf, int n) {
    es7210_ctx_t* e = (es7210_ctx_t*)((const mic_t*)self)->impl;
    if (!e || !e->i2s_open || e->initialized != 1) return 0;
    return sic_codec_read4(buf, n, 200);
}

static const struct mic_vtbl_s MIC_VT = { es_mic_start, es_mic_read };

/* ── Probe — always succeeds (no I2C / I2S at probe time) ──────────────── */

static int probe_mic(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "codec_es7210") != 0 || !d->cfg) return -1;

    const sic_es7210_cfg_t* cfg = (const sic_es7210_cfg_t*)d->cfg;
    memset(&g_es, 0, sizeof(g_es));
    g_es.i2c_bus     = cfg->i2c_bus;
    g_es.i2c_addr    = cfg->i2c_addr;
    g_es.pin_mclk    = cfg->pin_mclk;
    g_es.pin_bclk    = cfg->pin_bclk;
    g_es.pin_ws      = cfg->pin_ws;
    g_es.pin_dout    = cfg->pin_dout;
    g_es.pin_din     = cfg->pin_din;
    g_es.sample_rate = 16000;

    g_mic_inst.v    = &MIC_VT;
    g_mic_inst.impl = &g_es;
    *out = &g_mic_inst;
    return 0;
}

/* ── Driver registration ───────────────────────────────────────────────── */

static const sic_driver_t DRV_MIC = { "codec_es7210", SIC_F_MIC, probe_mic, NULL };

void sic_register_driver_codec_es7210(void) {
    sic_registry_register(&DRV_MIC);
}
