#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configuration passed via sic_board_ic_t.cfg for SIC_F_MIC entries that use
 * the "codec_es7210" driver hint (Tab5's 4-channel mic ADC).
 *
 * ES7210 is I2C address 0x40 on Tab5, sharing I2S0 with the ES8388 amp/DAC
 * (MCLK=30, BCLK=27, WS=29, DOUT=26, DIN=28).
 * Captures all 4 analog mic channels via 4-slot TDM (sic_codec_read4(), see
 * sic/audio.h and src/drivers/audio/codec_es7210.c's file header) -- unlike
 * M5Stack's own M5Unified library, which only activates 2 of the 4 channels.
 */
typedef struct sic_es7210_cfg_s {
    int     i2c_bus;    /* I2C bus index (0 = Tab5 internal bus) */
    uint8_t i2c_addr;   /* I2C address — 0x40 for ES7210          */
    int     pin_mclk;   /* MCLK GPIO (shared with ES8388)         */
    int     pin_bclk;   /* BCLK GPIO (shared with ES8388)         */
    int     pin_ws;     /* WS / LRCK GPIO (shared with ES8388)    */
    int     pin_dout;   /* DOUT GPIO (unused for MIC-only role, carried for
                          * symmetry with sic_codec_open()'s full-duplex
                          * signature — pass the same shared pin as the amp
                          * driver; this driver never writes to it). */
    int     pin_din;    /* DIN GPIO (ADC out -> ESP32, i.e. mic data in)   */
} sic_es7210_cfg_t;

#ifdef __cplusplus
}
#endif
