#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configuration for a simple PDM microphone wired directly to the MCU I2S
 * peripheral, such as Cardputer's SPM1423.
 */
typedef struct sic_pdm_mic_cfg_s {
    int clk_pin;       /* PDM clock GPIO */
    int data_pin;      /* PDM data GPIO  */
    int right_slot;    /* 1 = right slot, 0 = left slot */
} sic_pdm_mic_cfg_t;

#ifdef __cplusplus
}
#endif
