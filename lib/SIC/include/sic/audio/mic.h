#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct mic_vtbl_s {
    int (*start)(const void* self, int sample_rate_hz);  /* open I2S + init codec */
    int (*read) (const void* self, short* buf, int n);   /* returns frames read, <0 on error */
};

typedef struct mic_s {
    const struct mic_vtbl_s* v;
    void* impl;
} mic_t;

const mic_t* sic_mic(int index);

#ifdef __cplusplus
}
#endif
