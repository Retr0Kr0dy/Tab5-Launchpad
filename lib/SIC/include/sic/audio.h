#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Speaker */
int    sic_spk_open(int bclk_pin, int ws_pin, int dout_pin, int sample_rate_hz);
int    sic_spk_write(const int16_t* mono, size_t nframes);
void   sic_spk_close(void);
int    sic_spk_beep_1khz_ms(unsigned ms);

/* Microphone (PDM) */
int    sic_mic_open(int clk_pin, int data_pin, int sample_rate_hz, int right_slot);
int    sic_mic_read(int16_t* out_frames, size_t max_frames, int timeout_ms);
void   sic_mic_close(void);

/* Full-duplex I2S codec (e.g. ES8311) — TX+RX on the same peripheral with MCLK */
int    sic_codec_open(int mclk_pin, int bclk_pin, int ws_pin, int dout_pin, int din_pin, int sample_rate_hz);
int    sic_codec_read(int16_t* buf, int n, int timeout_ms);
int    sic_codec_write(const int16_t* mono, int n);
void   sic_codec_close(void);

/* Discards whatever is still queued in the TX DMA ring buffer, without a
 * full close/reopen (disable+re-enable is the standard ESP-IDF idiom for
 * this). For seeking a continuous audio stream: without it, not-yet-
 * transmitted audio queued before the seek keeps draining out of the DMA
 * buffer at the same time freshly-seeked audio gets written behind it --
 * audible as overlapping/chopped/pitched audio, not a clean cut. No-op on
 * backends that don't implement it (currently ESP-IDF only). */
void   sic_codec_flush(void);

/* Tab5-only: ES7210 mic, 4-slot TDM, on its own INDEPENDENT RX-only I2S
 * channel — deliberately NOT sharing a duplex pair with sic_codec_open()'s
 * TX (ES8388 speaker), even though both share the same physical
 * MCLK/BCLK/WS pins. An asymmetric-slot duplex pair (ES8388's 2-slot STD TX
 * + ES7210's 4-slot TDM RX from one i2s_new_channel() call) reliably left
 * RX's reads timing out with zero bytes, even with config matching the
 * reference bsp_audio_init() verbatim; isolating RX into its own channel
 * fixed it. Only one of sic_codec_open()'s TX or this RX channel may be
 * electrically active at a time (they share pins) — each function tears the
 * other down before claiming the pins, since Tab5 never needs simultaneous
 * record+playback. Only meaningful when built with SIC_TARGET_TAB5; not
 * implemented by the Arduino backend. */
int    sic_codec_mic4_open(int mclk_pin, int bclk_pin, int ws_pin, int din_pin, int sample_rate_hz);

/* Raw 4-slot TDM read off the channel sic_codec_mic4_open() opens. `buf`/`n`
 * are counted in int16 samples (not frames): the wire format is 4
 * interleaved slots per frame (MIC1,MIC2,MIC3,MIC4,...), so `n` must be a
 * multiple of 4 and the return value (samples actually read) always is too.
 * No downmix — unlike sic_codec_read(), callers get the raw per-mic
 * samples. */
int    sic_codec_read4(int16_t* buf, int n, int timeout_ms);

/* Helpers */
size_t sic_audio_upsample16to48(const int16_t* in, size_t n_in, int16_t* out, size_t out_cap);
void   sic_audio_postprocess(int16_t* s, size_t n);

#ifdef __cplusplus
}
#endif
