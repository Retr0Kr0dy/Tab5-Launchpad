#if defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)
#define SIC_AUDIO_NO_COMPAT 1
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>
extern "C" {
  #include "sic/sic.h"    /* includes sic/audio.h */
}

/*
 * sic_arduino_audio.cpp — Arduino/IDF I2S backend for sic/audio.h
 *
 * Three mutually exclusive modes share I2S_NUM_0:
 *   - PDM microphone  (sic_mic_*)   — RX only, PDM mode
 *   - Full-duplex codec (sic_codec_*) — TX+RX, standard I2S with MCLK (e.g. ES8311)
 *
 * A separate peripheral (I2S_NUM_1) is used for the raw speaker path (sic_spk_*).
 * Switching between PDM mic and codec automatically reinstalls the driver.
 *
 * All functions write mono audio and upmix to stereo internally because the ES8311
 * (and most I2S codecs) expect L+R frames even when only one channel carries audio.
 */

#ifndef SIC_AUDIO_DMA_LEN
#define SIC_AUDIO_DMA_LEN  256
#endif
#ifndef SIC_AUDIO_DMA_CNT
#define SIC_AUDIO_DMA_CNT  6
#endif

static bool g_spk   = false;
static bool g_mic   = false;
static bool g_codec = false;
static int  g_spk_sr = 48000;
static int  g_mic_sr = 16000;
static int  g_codec_sr = 16000;

/* ── Raw speaker (I2S_NUM_1, no MCLK, simple DAC/amp) ─────────────────────── */

int sic_spk_open(int bclk_pin, int ws_pin, int dout_pin, int sample_rate_hz) {
    if (g_spk) return 0;
    g_spk_sr = sample_rate_hz;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = sample_rate_hz;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_len          = SIC_AUDIO_DMA_LEN;
    cfg.dma_buf_count        = SIC_AUDIO_DMA_CNT;
    cfg.use_apll             = false;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;

    if (i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr) != ESP_OK) return -1;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = bclk_pin;
    pins.ws_io_num    = ws_pin;
    pins.data_out_num = dout_pin;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(I2S_NUM_1, &pins) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_1);
        return -2;
    }
    i2s_set_clk(I2S_NUM_1, sample_rate_hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    g_spk = true;
    return 0;
}

/* Upmix mono to stereo and write to I2S_NUM_1. Returns frames written. */
int sic_spk_write(const int16_t* mono, size_t nframes) {
    if (!g_spk || !mono || !nframes) return 0;
    static int16_t stereo[512 * 2];
    size_t written_frames = 0;
    while (nframes) {
        size_t chunk = nframes > 512 ? 512 : nframes;
        for (size_t i = 0; i < chunk; i++) {
            stereo[2 * i]     = mono[i];
            stereo[2 * i + 1] = mono[i];
        }
        size_t bytes = 0;
        if (i2s_write(I2S_NUM_1, stereo, chunk * 2 * sizeof(int16_t), &bytes, portMAX_DELAY) != ESP_OK) break;
        written_frames += bytes / (2 * sizeof(int16_t));
        mono    += chunk;
        nframes -= chunk;
    }
    return (int)written_frames;
}

void sic_spk_close(void) {
    if (!g_spk) return;
    i2s_driver_uninstall(I2S_NUM_1);
    g_spk = false;
}

/* Generate and play a 1kHz sine tone for `ms` milliseconds via sic_spk_*.
 *
 * Important: close the I2S speaker path after pushing a short zero tail.  Some
 * I2S amp paths keep reproducing whatever is still in DMA/shift registers if
 * the driver is left open after a finite tone, which feels like a stuck beep.
 */
int sic_spk_beep_1khz_ms(unsigned ms) {
    if (g_mic) { i2s_driver_uninstall(I2S_NUM_0); g_mic = false; }
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

    const size_t N = 512;
    static int16_t buf[N];
    const float w = 2.0f * PI * 1000.0f / (float)g_spk_sr;
    uint32_t total = (uint32_t)((uint64_t)g_spk_sr * ms / 1000);
    uint32_t phase = 0;

    while (total) {
        size_t n = total > N ? N : total;
        for (size_t i = 0; i < n; i++) {
            buf[i] = (int16_t)(sinf(w * (float)phase++) * 12000.0f);
        }
        sic_spk_write(buf, n);
        total -= n;
    }

    /* Drain with silence, then tear the bus down so the beep really stops. */
    memset(buf, 0, sizeof(buf));
    uint32_t zero_frames = (uint32_t)(g_spk_sr / 20); /* 50 ms */
    while (zero_frames) {
        size_t n = zero_frames > N ? N : zero_frames;
        sic_spk_write(buf, n);
        zero_frames -= n;
    }
    sic_spk_close();
    return 0;
}

/* ── PDM microphone (I2S_NUM_0, RX only) ──────────────────────────────────── */

int sic_mic_open(int clk_pin, int data_pin, int sample_rate_hz, int right_slot) {
    if (g_mic) return 0;
    if (g_spk) { i2s_driver_uninstall(I2S_NUM_1); g_spk = false; }
    g_mic_sr = sample_rate_hz;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate          = sample_rate_hz;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = right_slot ? I2S_CHANNEL_FMT_ONLY_RIGHT : I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_len          = SIC_AUDIO_DMA_LEN;
    cfg.dma_buf_count        = SIC_AUDIO_DMA_CNT;
    cfg.use_apll             = false;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return -1;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = I2S_PIN_NO_CHANGE;  /* PDM has no BCK */
    pins.ws_io_num    = clk_pin;            /* PDM clock on WS */
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = data_pin;

    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        return -2;
    }
    i2s_set_clk(I2S_NUM_0, sample_rate_hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    g_mic = true;
    return 0;
}

/* Returns frames read (got / sizeof(int16_t)), or 0 on timeout / error. */
int sic_mic_read(int16_t* out_frames, size_t max_frames, int timeout_ms) {
    if (!g_mic || !out_frames || !max_frames) return 0;
    size_t got = 0;
    i2s_read(I2S_NUM_0, out_frames, max_frames * sizeof(int16_t),
             &got, timeout_ms / portTICK_PERIOD_MS);
    return (int)(got / sizeof(int16_t));
}

void sic_mic_close(void) {
    if (!g_mic) return;
    i2s_driver_uninstall(I2S_NUM_0);
    g_mic = false;
}

/* ── Full-duplex I2S codec (I2S_NUM_0, TX+RX+MCLK — e.g. ES8311) ─────────── */

int sic_codec_open(int mclk_pin, int bclk_pin, int ws_pin, int dout_pin, int din_pin, int sample_rate_hz) {
    if (sample_rate_hz <= 0) sample_rate_hz = 16000;
    if (g_codec && g_codec_sr == sample_rate_hz) return 0;
    if (g_codec) { i2s_driver_uninstall(I2S_NUM_0); g_codec = false; }
    /* Codec and PDM mic share I2S_NUM_0 — reinstall with the new config. */
    if (g_mic) { i2s_driver_uninstall(I2S_NUM_0); g_mic = false; }

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    cfg.sample_rate          = sample_rate_hz;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_len          = SIC_AUDIO_DMA_LEN;
    cfg.dma_buf_count        = SIC_AUDIO_DMA_CNT;
    cfg.use_apll             = false;  /* APLL can fail on ESP32-S3 with some IDF versions */
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return -1;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = (mclk_pin >= 0) ? mclk_pin : I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = bclk_pin;
    pins.ws_io_num    = ws_pin;
    pins.data_out_num = dout_pin;
    pins.data_in_num  = din_pin;

    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        return -2;
    }
    i2s_set_clk(I2S_NUM_0, sample_rate_hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    g_codec = true;
    g_codec_sr = sample_rate_hz;
    return 0;
}

/* Return mono frames read from a stereo I2S codec. The hardware stream is
 * L/R interleaved; SIC's mic abstraction must not leak that detail, so this
 * downmixes to mono before returning to callers.
 */
int sic_codec_read(int16_t* buf, int n, int timeout_ms) {
    if (!g_codec || !buf || n <= 0) return 0;
    static int16_t stereo[512 * 2];
    int frames = 0;
    while (frames < n) {
        int want = (n - frames) > 512 ? 512 : (n - frames);
        size_t got = 0;
        if (i2s_read(I2S_NUM_0, stereo, (size_t)want * 2 * sizeof(int16_t),
                     &got, timeout_ms / portTICK_PERIOD_MS) != ESP_OK) break;
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

/* Upmix mono to stereo and write to codec TX. Returns frames written. */
int sic_codec_write(const int16_t* mono, int n) {
    if (!g_codec || !mono || n <= 0) return 0;
    static int16_t stereo[512 * 2];
    int written = 0;
    while (n > 0) {
        int chunk = n > 512 ? 512 : n;
        for (int i = 0; i < chunk; i++) {
            stereo[2 * i]     = mono[i];
            stereo[2 * i + 1] = mono[i];
        }
        size_t bytes = 0;
        /* On failure bytes stays 0, so `written` is not over-counted. */
        i2s_write(I2S_NUM_0, stereo, (size_t)chunk * 2 * sizeof(int16_t), &bytes, portMAX_DELAY);
        written += (int)(bytes / (2 * sizeof(int16_t)));
        mono += chunk;
        n    -= chunk;
    }
    return written;
}

void sic_codec_close(void) {
    if (!g_codec) return;
    i2s_driver_uninstall(I2S_NUM_0);
    g_codec = false;
}

#endif /* SIC_BACKEND_ARDUINO */
