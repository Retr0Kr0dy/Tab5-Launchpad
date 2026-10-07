#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct encoder_vtbl_s {
    int (*read_delta)(const void* self); /* returns signed click count since last call */
    int (*read_btn)(const void* self);   /* 1=pressed, 0=released, -1=no button */
};

typedef struct encoder_s {
    const struct encoder_vtbl_s* v;
    void* impl;
} encoder_t;

typedef struct sic_encoder_cfg_s {
    int pin_a;    /* CLK / A signal GPIO */
    int pin_b;    /* DT  / B signal GPIO */
    int pin_btn;  /* SW  / button GPIO (SIC_NOPIN if absent) */
} sic_encoder_cfg_t;

const encoder_t* sic_encoder(int index);

#ifdef __cplusplus
}
#endif
