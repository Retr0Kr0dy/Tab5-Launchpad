#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct sic_bq25896_cfg_s {
    int     i2c_bus;
    uint8_t i2c_addr; /* 0x6B */
} sic_bq25896_cfg_t;

#ifdef __cplusplus
}
#endif
