#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct sic_ir_tx_cfg_s {
    int pin;
    uint32_t carrier_hz; /* 0 = 38 kHz */
    uint8_t active_high; /* 1 = LED driver active high, 0 = active low */
} sic_ir_tx_cfg_t;

struct ir_vtbl_s {
    int (*send_nec)(const void* self, uint32_t code);
};

typedef struct ir_s {
    const struct ir_vtbl_s* v;
    void* impl;
} ir_t;

const ir_t* sic_ir_tx(int index);

#ifdef __cplusplus
}
#endif
