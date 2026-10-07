#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct sic_sd_sdmmc_cfg_s {
    int clk_pin, cmd_pin, d0_pin, d1_pin, d2_pin, d3_pin;
    int ldo_chan;     /* on-chip LDO channel powering the SD IO rail, or -1 to skip LDO management */
    uint32_t ldo_mv;  /* LDO output voltage in mV, ignored if ldo_chan < 0 */
} sic_sd_sdmmc_cfg_t;
#ifdef __cplusplus
}
#endif
