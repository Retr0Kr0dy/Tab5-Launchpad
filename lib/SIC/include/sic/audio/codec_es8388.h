#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configuration passed via sic_board_ic_t.cfg for SIC_F_AMP entries that use
 * the "codec_es8388" driver hint (Tab5's speaker/DAC path).
 *
 * ES8388 is I2C address 0x10 on Tab5, sharing I2S0 with the ES7210 mic ADC
 * (MCLK=30, BCLK=27, WS=29, DOUT=26, DIN=28).
 * Tab5 only uses this chip for playback; the ADC/mic side of ES8388's own
 * silicon is left unconfigured (ES7210 is the mic path on this board).
 */
typedef struct sic_es8388_cfg_s {
    int     i2c_bus;    /* I2C bus index (0 = Tab5 internal bus) */
    uint8_t i2c_addr;   /* I2C address — 0x10 for ES8388          */
    int     pin_mclk;   /* MCLK GPIO (shared with ES7210)         */
    int     pin_bclk;   /* BCLK GPIO (shared with ES7210)         */
    int     pin_ws;     /* WS / LRCK GPIO (shared with ES7210)    */
    int     pin_dout;   /* DOUT GPIO (DAC out -> speaker)         */
    int     pin_din;    /* DIN GPIO (unused for AMP-only role, carried for
                          * symmetry with sic_codec_open()'s full-duplex
                          * signature — pass the same shared pin as the mic
                          * driver; this driver never reads from it). */
} sic_es8388_cfg_t;

#ifdef __cplusplus
}
#endif
