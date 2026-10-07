/*
 * sic_espidf_audio.c — ESP-IDF v5 I2S backend for sic/audio.h
 *
 * Functional twin of src/backends/arduino/sic_arduino_audio.cpp, ported from
 * the legacy `driver/i2s.h` driver to the ESP-IDF v5 channel-handle driver
 * (`driver/i2s_std.h` + `driver/i2s_pdm.h`).  The legacy driver is not built
 * for the ESP32-P4 at all, which is the whole reason this file exists.
 *
 * Three modes, same peripheral allocation as the Arduino backend:
 *   - Raw speaker    (sic_spk_*)   — I2S_NUM_1, TX only, no MCLK (DAC/amp path)
 *   - PDM microphone (sic_mic_*)   — I2S_NUM_0, RX only, PDM mode
 *   - Full-duplex codec (sic_codec_*) — I2S_NUM_0, TX+RX, standard I2S with
 *                                    MCLK (e.g. ES8311)
 *
 * The PDM mic and the codec share I2S_NUM_0 and are therefore mutually
 * exclusive: opening one tears the other down first.  The beep helper and
 * sic_mic_open() additionally tear down the *other* peripheral, exactly as the
 * Arduino backend does, so the two backends stay observably identical.
 *
 * All TX paths write mono audio and upmix to stereo internally because the
 * ES8311 (and most I2S codecs) expect L+R frames even when only one channel
 * carries audio.  The codec RX path downmixes L/R back to mono so the stereo
 * wire format never leaks through sic/audio.h.
 *
 * TDM / multi-slot capture: Tab5's ES7210 4-channel mic ADC does not fit
 * sic_codec_open()/sic_codec_read()'s stereo-only contract, so on
 * SIC_TARGET_TAB5 builds codec RX is reconfigured for 4-slot TDM instead of
 * 2-slot STD stereo (see the SIC_TARGET_TAB5 branch in sic_codec_open()
 * below) and exposed through the separate sic_codec_read4() (raw, no
 * downmix — see sic/audio.h). Codec TX (ES8388 playback) is untouched by
 * this — same STD stereo path as ever, confirmed via the real M5Tab5-UserDemo
 * reference (`bsp_audio_init()` in m5stack_tab5.c), which does the exact
 * same TX=STD/RX=TDM split on one shared duplex channel pair. Other boards'
 * builds (no SIC_TARGET_TAB5) get the original STD/STD behavior, unchanged.
 */
#if defined(SIC_BACKEND_ESPIDF)

#define SIC_AUDIO_NO_COMPAT 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#if defined(SIC_TARGET_TAB5)
#include "driver/i2s_tdm.h"
#endif
#include "esp_err.h"
#include "esp_log.h"

#include "sic/audio.h"

static const char *TAG = "sic_audio";

/*
 * DMA sizing.  The legacy driver's dma_buf_len/dma_buf_count became
 * i2s_chan_config_t::dma_frame_num / ::dma_desc_num in v5; the tunable names
 * are kept identical to the Arduino backend so a board can override both
 * backends with one -D flag.
 */
#ifndef SIC_AUDIO_DMA_LEN
#define SIC_AUDIO_DMA_LEN  256   /* frames per DMA descriptor (dma_frame_num) */
#endif
#ifndef SIC_AUDIO_DMA_CNT
#define SIC_AUDIO_DMA_CNT  6     /* number of DMA descriptors (dma_desc_num)  */
#endif

/* Bounded scratch chunk, in frames — same 512-frame convention as Arduino.
 * Every TX/RX path chunks through a static buffer of this size rather than
 * allocating per call. */
#define SIC_AUDIO_CHUNK    512
#define SIC_BEEP_N         SIC_AUDIO_CHUNK   /* mono tone buffer, in frames */

#define SIC_AUDIO_PI       3.14159265358979323846f

static i2s_chan_handle_t g_spk_tx   = NULL;
static i2s_chan_handle_t g_mic_rx   = NULL;
static i2s_chan_handle_t g_codec_tx = NULL;
static i2s_chan_handle_t g_codec_rx = NULL;

static int g_spk_sr   = 48000;
static int g_mic_sr   = 16000;   /* bookkeeping only, as in the Arduino backend */
static int g_codec_sr = 16000;

#if defined(SIC_TARGET_TAB5)
/* Tab5 ES7210 mic: independent RX-only channel, NOT sharing a duplex pair
 * with ES8388's TX -- see sic_codec_mic4_open()'s header comment for why. */
static i2s_chan_handle_t g_tab5_mic_rx = NULL;
static int g_tab5_mic_sr = 48000;
#endif

/* ── Shared helpers ───────────────────────────────────────────────────────── */

/* i2s_*_gpio_config_t members are gpio_num_t; unwired signals use the v5
 * sentinel I2S_GPIO_UNUSED (== GPIO_NUM_NC), which replaced the legacy
 * driver's I2S_PIN_NO_CHANGE. */
static gpio_num_t sic_i2s_pin(int pin)
{
    return (pin >= 0) ? (gpio_num_t)pin : I2S_GPIO_UNUSED;
}

/* v5 read/write take a timeout in *milliseconds*, not ticks (the legacy driver
 * took ticks, hence the `/ portTICK_PERIOD_MS` in the Arduino backend).  A
 * negative timeout from a caller means "block forever". */
static uint32_t sic_i2s_timeout(int timeout_ms)
{
    return (timeout_ms < 0) ? (uint32_t)portMAX_DELAY : (uint32_t)timeout_ms;
}

/* Disable-then-delete: i2s_del_channel() is only legal from REGISTERED/READY,
 * so a running channel must be disabled first.  Both calls are best-effort —
 * teardown must never leave a stale handle behind. */
static void sic_i2s_release(i2s_chan_handle_t *h)
{
    if (*h == NULL) return;
    (void)i2s_channel_disable(*h);
    (void)i2s_del_channel(*h);
    *h = NULL;
}

static i2s_chan_config_t sic_i2s_chan_cfg(i2s_port_t port)
{
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    cfg.dma_desc_num  = SIC_AUDIO_DMA_CNT;
    cfg.dma_frame_num = SIC_AUDIO_DMA_LEN;
    return cfg;
}

/* ── Raw speaker (I2S_NUM_1, no MCLK, simple DAC/amp) ─────────────────────── */

int sic_spk_open(int bclk_pin, int ws_pin, int dout_pin, int sample_rate_hz)
{
    if (g_spk_tx) return 0;
    g_spk_sr = sample_rate_hz;

    i2s_chan_config_t chan_cfg = sic_i2s_chan_cfg(I2S_NUM_1);
    esp_err_t spk_rc = i2s_new_channel(&chan_cfg, &g_spk_tx, NULL);
    if (spk_rc != ESP_OK) {
        ESP_LOGE(TAG, "sic_spk_open: i2s_new_channel failed rc=%d", (int)spk_rc);
        g_spk_tx = NULL;
        return -1;
    }

    /* Philips framing == the legacy I2S_COMM_FORMAT_STAND_I2S, and stereo
     * slots == I2S_CHANNEL_FMT_RIGHT_LEFT: sic_spk_write() upmixes to L+R. */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,          /* raw amp path has no MCLK */
            .bclk = sic_i2s_pin(bclk_pin),
            .ws   = sic_i2s_pin(ws_pin),
            .dout = sic_i2s_pin(dout_pin),
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };

    if (i2s_channel_init_std_mode(g_spk_tx, &std_cfg) != ESP_OK) {
        sic_i2s_release(&g_spk_tx);
        return -2;
    }
    /* No legacy i2s_set_clk() equivalent is needed: rate/width/slot-mode are
     * all part of std_cfg.  Enabling is a separate step in v5, though. */
    if (i2s_channel_enable(g_spk_tx) != ESP_OK) {
        sic_i2s_release(&g_spk_tx);
        return -3;
    }
    return 0;
}

/* Upmix mono to stereo and write to I2S_NUM_1. Returns frames written. */
int sic_spk_write(const int16_t* mono, size_t nframes)
{
    if (!g_spk_tx || !mono || !nframes) return 0;
    static int16_t stereo[SIC_AUDIO_CHUNK * 2];
    size_t written_frames = 0;
    while (nframes) {
        size_t chunk = nframes > SIC_AUDIO_CHUNK ? (size_t)SIC_AUDIO_CHUNK : nframes;
        for (size_t i = 0; i < chunk; i++) {
            stereo[2 * i]     = mono[i];
            stereo[2 * i + 1] = mono[i];
        }
        size_t bytes = 0;
        if (i2s_channel_write(g_spk_tx, stereo, chunk * 2 * sizeof(int16_t),
                              &bytes, (uint32_t)portMAX_DELAY) != ESP_OK) break;
        written_frames += bytes / (2 * sizeof(int16_t));
        mono    += chunk;
        nframes -= chunk;
    }
    return (int)written_frames;
}

void sic_spk_close(void)
{
    if (!g_spk_tx) return;
    sic_i2s_release(&g_spk_tx);
}

/* Generate and play a 1kHz sine tone for `ms` milliseconds via sic_spk_*.
 *
 * Important: close the I2S speaker path after pushing a short zero tail.  Some
 * I2S amp paths keep reproducing whatever is still in DMA/shift registers if
 * the driver is left open after a finite tone, which feels like a stuck beep.
 */
int sic_spk_beep_1khz_ms(unsigned ms)
{
    if (g_mic_rx) { sic_i2s_release(&g_mic_rx); }
    if (ms == 0) ms = 1;

    if (sic_spk_open(
            #ifdef SIC_SPK_BCLK
              SIC_SPK_BCLK,
            #else
              -1,
            #endif
            #ifdef SIC_SPK_WS
              SIC_SPK_WS,
            #else
              -1,
            #endif
            #ifdef SIC_SPK_DOUT
              SIC_SPK_DOUT,
            #else
              -1,
            #endif
            g_spk_sr) != 0) return -1;

    static int16_t buf[SIC_BEEP_N];
    const float w = 2.0f * SIC_AUDIO_PI * 1000.0f / (float)g_spk_sr;
    uint32_t total = (uint32_t)((uint64_t)g_spk_sr * ms / 1000);
    uint32_t phase = 0;

    while (total) {
        size_t n = total > SIC_BEEP_N ? (size_t)SIC_BEEP_N : (size_t)total;
        for (size_t i = 0; i < n; i++) {
            buf[i] = (int16_t)(sinf(w * (float)phase++) * 12000.0f);
        }
        sic_spk_write(buf, n);
        total -= (uint32_t)n;
    }

    /* Drain with silence, then tear the bus down so the beep really stops. */
    memset(buf, 0, sizeof(buf));
    uint32_t zero_frames = (uint32_t)(g_spk_sr / 20); /* 50 ms */
    while (zero_frames) {
        size_t n = zero_frames > SIC_BEEP_N ? (size_t)SIC_BEEP_N : (size_t)zero_frames;
        sic_spk_write(buf, n);
        zero_frames -= (uint32_t)n;
    }
    sic_spk_close();
    return 0;
}

/* ── PDM microphone (I2S_NUM_0, RX only) ──────────────────────────────────── */

int sic_mic_open(int clk_pin, int data_pin, int sample_rate_hz, int right_slot)
{
    if (g_mic_rx) return 0;
    if (g_spk_tx) { sic_i2s_release(&g_spk_tx); }
    g_mic_sr = sample_rate_hz;

    i2s_chan_config_t chan_cfg = sic_i2s_chan_cfg(I2S_NUM_0);
    esp_err_t mic_rc = i2s_new_channel(&chan_cfg, NULL, &g_mic_rx);
    if (mic_rc != ESP_OK) {
        ESP_LOGE(TAG, "sic_mic_open: i2s_new_channel failed rc=%d", (int)mic_rc);
        g_mic_rx = NULL;
        return -1;
    }

    /* PDM RX is its own mode in v5 (driver/i2s_pdm.h).  The legacy
     * I2S_CHANNEL_FMT_ONLY_RIGHT / _ONLY_LEFT pair becomes slot_mode MONO plus
     * an explicit slot_mask of I2S_PDM_SLOT_RIGHT / I2S_PDM_SLOT_LEFT.  PDM has
     * no BCLK pin: the PDM clock goes out on .clk (the legacy backend routed it
     * through ws_io_num for the same reason). */
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG((uint32_t)sample_rate_hz),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = sic_i2s_pin(clk_pin),
            .din = sic_i2s_pin(data_pin),
            .invert_flags = { .clk_inv = 0 },
        },
    };
    pdm_cfg.slot_cfg.slot_mask = right_slot ? I2S_PDM_SLOT_RIGHT : I2S_PDM_SLOT_LEFT;

    if (i2s_channel_init_pdm_rx_mode(g_mic_rx, &pdm_cfg) != ESP_OK) {
        sic_i2s_release(&g_mic_rx);
        return -2;
    }
    if (i2s_channel_enable(g_mic_rx) != ESP_OK) {
        sic_i2s_release(&g_mic_rx);
        return -3;
    }
    return 0;
}

/* Returns frames read (got / sizeof(int16_t)), or 0 on timeout / error.
 * PDM RX is mono on the wire, so no downmix is needed here. */
int sic_mic_read(int16_t* out_frames, size_t max_frames, int timeout_ms)
{
    if (!g_mic_rx || !out_frames || !max_frames) return 0;
    size_t got = 0;
    (void)i2s_channel_read(g_mic_rx, out_frames, max_frames * sizeof(int16_t),
                           &got, sic_i2s_timeout(timeout_ms));
    return (int)(got / sizeof(int16_t));
}

void sic_mic_close(void)
{
    if (!g_mic_rx) return;
    sic_i2s_release(&g_mic_rx);
}

/* ── Full-duplex I2S codec (I2S_NUM_0, TX+RX+MCLK — e.g. ES8311) ─────────── */

int sic_codec_open(int mclk_pin, int bclk_pin, int ws_pin, int dout_pin, int din_pin, int sample_rate_hz)
{
    if (sample_rate_hz <= 0) sample_rate_hz = 16000;
    if (g_codec_tx && g_codec_sr == sample_rate_hz) return 0;
    if (g_codec_tx || g_codec_rx) {
        sic_i2s_release(&g_codec_tx);
        sic_i2s_release(&g_codec_rx);
    }
    /* Codec and PDM mic share I2S_NUM_0 — reinstall with the new config. */
    if (g_mic_rx) { sic_i2s_release(&g_mic_rx); }
#if defined(SIC_TARGET_TAB5)
    /* Tab5 playback is ES8388 TX only. Its microphone is the separate ES7210
     * path below, so allocating/enabling an otherwise-unused standard-I2S RX
     * channel here burns DMA descriptors/interrupts and competes with video
     * decode for no benefit. The ES8388 amp driver explicitly documents its
     * DIN pin as unused in the AMP-only role. */
    if (g_tab5_mic_rx) { sic_i2s_release(&g_tab5_mic_rx); }
#endif

    i2s_chan_config_t chan_cfg = sic_i2s_chan_cfg(I2S_NUM_0);
#if defined(SIC_TARGET_TAB5)
    esp_err_t codec_tx_rc = i2s_new_channel(&chan_cfg, &g_codec_tx, NULL);
    if (codec_tx_rc != ESP_OK) {
        /* A failed i2s_new_channel() here is easy to miss downstream (it
         * can surface only as a terse "have_audio=0" several layers up):
         * log it explicitly. Common cause is a caller that opens this
         * channel late in its own startup sequence, after other
         * internal-RAM-hungry subsystems (a hardware video decoder, DMA
         * descriptor pools, etc.) have already fragmented internal RAM
         * below what this channel's own DMA descriptors need. */
        ESP_LOGE(TAG, "sic_codec_open: i2s_new_channel (TX) failed rc=%d", (int)codec_tx_rc);
        g_codec_tx = NULL;
        return -1;
    }
#else
    /* Other boards retain the original full-duplex codec contract. */
    esp_err_t codec_rc = i2s_new_channel(&chan_cfg, &g_codec_tx, &g_codec_rx);
    if (codec_rc != ESP_OK) {
        ESP_LOGE(TAG, "sic_codec_open: i2s_new_channel failed rc=%d", (int)codec_rc);
        g_codec_tx = NULL;
        g_codec_rx = NULL;
        return -1;
    }
#endif

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = sic_i2s_pin(mclk_pin),
            .bclk = sic_i2s_pin(bclk_pin),
            .ws   = sic_i2s_pin(ws_pin),
            .dout = sic_i2s_pin(dout_pin),
#if defined(SIC_TARGET_TAB5)
            .din  = I2S_GPIO_UNUSED,
#else
            .din  = sic_i2s_pin(din_pin),
#endif
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };
#if defined(SIC_TARGET_TAB5)
    (void)din_pin;
    if (i2s_channel_init_std_mode(g_codec_tx, &std_cfg) != ESP_OK) {
        sic_i2s_release(&g_codec_tx);
        return -2;
    }
    if (i2s_channel_enable(g_codec_tx) != ESP_OK) {
        sic_i2s_release(&g_codec_tx);
        return -3;
    }
#else
    if (i2s_channel_init_std_mode(g_codec_tx, &std_cfg) != ESP_OK ||
        i2s_channel_init_std_mode(g_codec_rx, &std_cfg) != ESP_OK) {
        sic_i2s_release(&g_codec_tx);
        sic_i2s_release(&g_codec_rx);
        return -2;
    }
    if (i2s_channel_enable(g_codec_tx) != ESP_OK ||
        i2s_channel_enable(g_codec_rx) != ESP_OK) {
        sic_i2s_release(&g_codec_tx);
        sic_i2s_release(&g_codec_rx);
        return -3;
    }
#endif

    g_codec_sr = sample_rate_hz;
    return 0;
}

/* Return mono frames read from a stereo I2S codec. The hardware stream is
 * L/R interleaved; SIC's mic abstraction must not leak that detail, so this
 * downmixes to mono before returning to callers.
 */
int sic_codec_read(int16_t* buf, int n, int timeout_ms)
{
    if (!g_codec_rx || !buf || n <= 0) return 0;
    static int16_t stereo[SIC_AUDIO_CHUNK * 2];
    int frames = 0;
    while (frames < n) {
        int want = (n - frames) > SIC_AUDIO_CHUNK ? SIC_AUDIO_CHUNK : (n - frames);
        size_t got = 0;
        if (i2s_channel_read(g_codec_rx, stereo, (size_t)want * 2 * sizeof(int16_t),
                             &got, sic_i2s_timeout(timeout_ms)) != ESP_OK) break;
        int got_frames = (int)(got / (2 * sizeof(int16_t)));
        if (got_frames <= 0) break;
        for (int i = 0; i < got_frames; ++i) {
            int32_t l = stereo[2 * i];
            int32_t r = stereo[2 * i + 1];
            buf[frames + i] = (int16_t)((l + r) / 2);
        }
        frames += got_frames;
        if (got_frames < want) break;
    }
    return frames;
}

#if defined(SIC_TARGET_TAB5)
/*
 * sic_codec_mic4_open() — Tab5 ES7210 mic, INDEPENDENT RX-only channel.
 *
 * A shared TX(STD 2-slot, ES8388)/RX(TDM 4-slot, ES7210) full-duplex pair
 * from one i2s_new_channel() call -- matching the reference bsp_audio_init()
 * design byte-for-byte, including call order -- reliably leaves RX's
 * i2s_channel_read() timing out with zero bytes on every call, independent
 * of sample rate. Isolating RX into its own solo channel (i2s_new_channel
 * with tx=NULL) fixes it: an asymmetric-slot-count duplex pair (2-slot STD
 * TX + 4-slot TDM RX sharing one port) does not work on this hardware/IDF
 * combination.
 *
 * ES8388 (TX, speaker) and this RX channel physically share MCLK/BCLK/WS
 * (GPIO30/27/29) — since they're no longer one duplex pair, only one of
 * them may be electrically driving those pins as I2S_ROLE_MASTER at a time.
 * sic_codec_open() above releases g_tab5_mic_rx before claiming the pins for
 * TX; this function releases g_codec_tx/g_codec_rx before claiming them for
 * RX. Neither direction is ever needed simultaneously on Tab5 in practice
 * (no feature does simultaneous record+playback), so this time-multiplexed
 * hand-off is a real fix, not a workaround for a missing feature.
 */
int sic_codec_mic4_open(int mclk_pin, int bclk_pin, int ws_pin, int din_pin, int sample_rate_hz)
{
    if (sample_rate_hz <= 0) sample_rate_hz = 48000;
    if (g_tab5_mic_rx && g_tab5_mic_sr == sample_rate_hz) return 0;
    if (g_tab5_mic_rx) { sic_i2s_release(&g_tab5_mic_rx); }
    if (g_codec_tx || g_codec_rx) {
        sic_i2s_release(&g_codec_tx);
        sic_i2s_release(&g_codec_rx);
    }

    i2s_chan_config_t chan_cfg = sic_i2s_chan_cfg(I2S_NUM_0);
    chan_cfg.auto_clear = true;
    esp_err_t mic4_rc = i2s_new_channel(&chan_cfg, NULL, &g_tab5_mic_rx);
    if (mic4_rc != ESP_OK) {
        ESP_LOGE(TAG, "sic_codec_mic4_open: i2s_new_channel failed rc=%d", (int)mic4_rc);
        g_tab5_mic_rx = NULL;
        return -1;
    }

    /* 4-slot TDM, matching the real reference's bsp_audio_init() RX config
     * exactly (clk_cfg mclk_multiple=256/bclk_div=8, slot_mask = all 4
     * slots, total_slot auto). ES7210's own SDP_INTERFACE2 register must
     * independently be set to TDM mode too (codec_es7210.c) to match. */
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)sample_rate_hz,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
            .bclk_div       = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            .slot_mask      = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                                     I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width       = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol         = false,
            .bit_shift      = true,
            .left_align     = false,
            .big_endian     = false,
            .bit_order_lsb  = false,
            .skip_mask      = false,
            .total_slot     = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = sic_i2s_pin(mclk_pin),
            .bclk = sic_i2s_pin(bclk_pin),
            .ws   = sic_i2s_pin(ws_pin),
            .dout = I2S_GPIO_UNUSED,
            .din  = sic_i2s_pin(din_pin),
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };
    if (i2s_channel_init_tdm_mode(g_tab5_mic_rx, &tdm_cfg) != ESP_OK) {
        sic_i2s_release(&g_tab5_mic_rx);
        return -2;
    }
    if (i2s_channel_enable(g_tab5_mic_rx) != ESP_OK) {
        sic_i2s_release(&g_tab5_mic_rx);
        return -3;
    }

    g_tab5_mic_sr = sample_rate_hz;
    return 0;
}
#endif

/* Raw 4-slot TDM read (Tab5 ES7210 mic, SIC_TARGET_TAB5 only) — see
 * sic/audio.h. No downmix: buf receives interleaved MIC1..MIC4 samples
 * as-is. n/return value are in int16 samples, must be a multiple of 4. */
int sic_codec_read4(int16_t* buf, int n, int timeout_ms)
{
#if defined(SIC_TARGET_TAB5)
    if (!g_tab5_mic_rx || !buf || n < 4) return 0;
    int want_frames = n / 4;
    size_t got_bytes = 0;
    esp_err_t err = i2s_channel_read(g_tab5_mic_rx, buf, (size_t)want_frames * 4 * sizeof(int16_t),
                                     &got_bytes, sic_i2s_timeout(timeout_ms));
    if (err != ESP_OK) return 0;
    return (int)(got_bytes / sizeof(int16_t));
#else
    (void)buf; (void)n; (void)timeout_ms;
    return 0;
#endif
}

/* Upmix mono to stereo and write to codec TX. Returns frames written. */
int sic_codec_write(const int16_t* mono, int n)
{
    if (!g_codec_tx || !mono || n <= 0) return 0;
    static int16_t stereo[SIC_AUDIO_CHUNK * 2];
    int written = 0;
    while (n > 0) {
        int chunk = n > SIC_AUDIO_CHUNK ? SIC_AUDIO_CHUNK : n;
        for (int i = 0; i < chunk; i++) {
            stereo[2 * i]     = mono[i];
            stereo[2 * i + 1] = mono[i];
        }
        size_t bytes = 0;
        /* On failure bytes stays 0, so `written` is not over-counted. */
        (void)i2s_channel_write(g_codec_tx, stereo, (size_t)chunk * 2 * sizeof(int16_t),
                                &bytes, (uint32_t)portMAX_DELAY);
        written += (int)(bytes / (2 * sizeof(int16_t)));
        mono += chunk;
        n    -= chunk;
    }
    return written;
}

void sic_codec_close(void)
{
    if (!g_codec_tx && !g_codec_rx) return;
    sic_i2s_release(&g_codec_tx);
    sic_i2s_release(&g_codec_rx);
}

void sic_codec_flush(void)
{
    if (!g_codec_tx) return;
    i2s_channel_disable(g_codec_tx);
    i2s_channel_enable(g_codec_tx);

    /* disable+enable alone stops transmission but does NOT clear the DMA
     * ring's memory -- i2s_channel_enable() just restarts the state machine
     * consuming whatever bytes are still physically sitting in those
     * dma_desc_num descriptors, the LAST real samples written before this
     * flush. With nothing new written yet (a caller flushing specifically
     * because it wants silence until it has something new to say, e.g. a
     * seek), the hardware clocks that stale content out in a loop, once per
     * ring traversal -- audible as the old audio's tail looping/stuttering,
     * not silence. Overwriting the entire ring with real zero samples here
     * guarantees any such replay is silence instead. Sized to the full ring
     * plus one descriptor of headroom so every descriptor is provably
     * overwritten regardless of where the DMA's read pointer sits. */
    static const int16_t zero[SIC_AUDIO_CHUNK * 2] = {0};
    int frames_left = SIC_AUDIO_DMA_CNT * SIC_AUDIO_DMA_LEN + SIC_AUDIO_DMA_LEN;
    while (frames_left > 0) {
        int chunk = frames_left > SIC_AUDIO_CHUNK ? SIC_AUDIO_CHUNK : frames_left;
        size_t bytes = 0;
        if (i2s_channel_write(g_codec_tx, zero, (size_t)chunk * 2 * sizeof(int16_t),
                              &bytes, (uint32_t)portMAX_DELAY) != ESP_OK) {
            break;
        }
        frames_left -= chunk;
    }
}

#endif /* SIC_BACKEND_ESPIDF */
