#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configuration for an always-on I2S speaker amp path such as Cardputer's
 * NS4168.  Some amps have no separate shutdown GPIO exposed; in that case
 * enable() is intentionally a logical no-op while the speaker path itself is
 * driven through sic_spk_*.
 */
typedef struct sic_i2s_amp_cfg_s {
    int bclk_pin;
    int ws_pin;
    int dout_pin;
} sic_i2s_amp_cfg_t;

#ifdef __cplusplus
}
#endif
