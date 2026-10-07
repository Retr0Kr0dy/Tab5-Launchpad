#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

struct amp_vtbl_s {
    void (*enable)(const void* self, int on);  /* 1 = unmute DAC + power amp, 0 = mute */

    /* Optional high-level playback hooks.  Drivers that only expose a mute or
     * power GPIO may leave these NULL; direct-I2S amp drivers should implement
     * them so examples/apps never need board-specific speaker pins.
     */
    int  (*play_mono)(const void* self, const int16_t* mono, size_t nframes, int sample_rate_hz);
    int  (*beep_ms)(const void* self, unsigned ms);

    /* Optional low-level streaming hooks:
     * play_mono() pads ~100ms of silence onto the end of every single call,
     * fine for a one-shot beep/notification but produces an audible
     * click/gap if called once per small decoded-audio chunk in a
     * continuous stream. stream_open() prepares the I2S channel and unmutes
     * once; stream_write() is a plain blocking write with no padding,
     * called repeatedly per chunk; stream_close() writes the same trailing
     * silence play_mono() does (avoids an abrupt pop) and re-mutes. Leave
     * NULL on drivers with no continuous-streaming use case -- callers must
     * check for NULL same as the other optional hooks. */
    int  (*stream_open)(const void* self, int sample_rate_hz);
    int  (*stream_write)(const void* self, const int16_t* mono, size_t nframes);
    void (*stream_close)(const void* self);

    /* Optional hardware volume control: a real DAC digital-volume register
     * write (0-100 -> 0dB..-96dB, matching
     * M5Tab5-UserDemo's own bsp_codec_set_volume()/esp_codec_dev_set_out_vol()
     * path for this exact chip -- see codec_es8388.c's set_volume for the
     * register math). Leave NULL on drivers with no such register; callers
     * must check for NULL same as the other optional hooks. */
    void (*set_volume)(const void* self, int percent);
    int  (*get_volume)(const void* self); /* last value passed to set_volume, or a driver default */

    /* Discards whatever is still queued in the TX path (see
     * sic_codec_flush()'s doc comment) -- for seeking a continuous stream,
     * where old not-yet-transmitted audio overlapping with freshly-seeked
     * audio is audible as a garbled double-write, not a clean cut. Leave
     * NULL on drivers with no such primitive; callers must check for NULL
     * same as the other optional hooks. */
    void (*flush)(const void* self);

    /* Pure register-level DAC mute: a single I2C write to the codec chip,
     * touching nothing on the I2S/DMA side. Needed because exiting a
     * playback session while its audio producer/consumer tasks are stuck
     * (e.g. a slow SD read that outlasts a stop-wait timeout) can skip the
     * normal stream_close()-based mute entirely, since that path also tears
     * down I2S state a still-running stuck task might be touching --
     * leaving the DAC live and stale DMA content looping indefinitely.
     * mute() is safe to call unconditionally, even while another task owns
     * the I2S channel, specifically so a caller can silence hardware output
     * immediately on exit before it's known whether the stop sequence will
     * complete cleanly or get abandoned as stuck. Leave NULL on drivers
     * with no such primitive; callers must check for NULL same as the
     * other optional hooks. */
    void (*mute)(const void* self);
};

typedef struct amp_s {
    const struct amp_vtbl_s* v;
    void* impl;
} amp_t;

const amp_t* sic_amp(int index);

#ifdef __cplusplus
}
#endif
