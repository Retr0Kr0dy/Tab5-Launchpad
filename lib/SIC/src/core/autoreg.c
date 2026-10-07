/*
 * autoreg.c — Conditional registration of built-in SIC drivers.
 *
 * By default every driver whose source file is compiled gets registered.
 * Use -DSIC_NO_DRV_<NAME> to exclude a driver even if its source compiles.
 * Use -DSIC_DRV_ONLY together with -DSIC_DRV_<NAME> for explicit opt-in
 * (advanced: disables all defaults).
 *
 * To add a new driver: add an #ifdef block below and call its register fn.
 */

#include "sic/sic_registry.h"

/* ── Opt-out / opt-in resolution ─────────────────────────────────────── */

#if !defined(SIC_DRV_ONLY)
    /* Default: enable all known drivers unless explicitly suppressed. */
#   if !defined(SIC_NO_DRV_KBD_74HC138)
#       define SIC__AUTOREG_KBD_74HC138
#   endif
#   if !defined(SIC_NO_DRV_TP4057)
#       define SIC__AUTOREG_TP4057
#   endif
#   if defined(SIC_DRV_BQ25896) || defined(SIC_TARGET_TPAGER)
#       define SIC__AUTOREG_BQ25896
#   endif
    /* TCA8418 is auto-enabled for boards that declare it, otherwise opt-in. */
#   if defined(SIC_DRV_KBD_TCA8418) || defined(SIC_TARGET_TPAGER) || defined(SIC_TARGET_CARDPUTER_ADV)
#       define SIC__AUTOREG_KBD_TCA8418
#   endif
#   if defined(SIC_DRV_ENCODER_GPIO)
#       define SIC__AUTOREG_ENCODER_GPIO
#   endif
#   if defined(SIC_DRV_CODEC_ES8311)
#       define SIC__AUTOREG_CODEC_ES8311
#   endif
#   if defined(SIC_DRV_MIC_PDM) || defined(SIC_TARGET_CARDPUTER)
#       define SIC__AUTOREG_MIC_PDM
#   endif
#   if defined(SIC_DRV_AMP_I2S) || defined(SIC_TARGET_CARDPUTER)
#       define SIC__AUTOREG_AMP_I2S
#   endif
#   if defined(SIC_DRV_IR_GPIO) || defined(SIC_TARGET_CARDPUTER)
#       define SIC__AUTOREG_IR_GPIO
#   endif
#   if defined(SIC_DRV_SD_SPI) || defined(SIC_TARGET_CARDPUTER) || defined(SIC_TARGET_TPAGER)
#       define SIC__AUTOREG_SD_SPI
#   endif
    /* Tab5 (Phases 3-6): pure-C99 drivers auto-enable for SIC_TARGET_TAB5,
     * same opt-out convention as every other target above. */
#   if defined(SIC_DRV_TOUCH_GT911) || defined(SIC_TARGET_TAB5)
#       define SIC__AUTOREG_TOUCH_GT911
#   endif
#   if defined(SIC_DRV_TOUCH_ST712X) || defined(SIC_TARGET_TAB5)
#       define SIC__AUTOREG_TOUCH_ST712X
#   endif
#   if defined(SIC_DRV_RTC_RX8130) || defined(SIC_TARGET_TAB5)
#       define SIC__AUTOREG_RTC_RX8130
#   endif
#   if defined(SIC_DRV_IMU_BMI270) || defined(SIC_TARGET_TAB5)
#       define SIC__AUTOREG_IMU_BMI270
#   endif
#   if defined(SIC_DRV_TAB5_CHGCTL) || defined(SIC_TARGET_TAB5)
#       define SIC__AUTOREG_TAB5_CHGCTL
#   endif
#   if defined(SIC_DRV_CODEC_ES8388) || defined(SIC_TARGET_TAB5)
#       define SIC__AUTOREG_CODEC_ES8388
#   endif
#   if defined(SIC_DRV_CODEC_ES7210) || defined(SIC_TARGET_TAB5)
#       define SIC__AUTOREG_CODEC_ES7210
#   endif
    /* ESP-IDF-only Tab5 drivers (sd_sdmmc_espidf.c / sic_espidf_camera.c
     * compile to nothing without SIC_BACKEND_ESPIDF — see those files'
     * `#if defined(SIC_BACKEND_ESPIDF)` guards), so gate on the backend too,
     * not just the target, to keep a non-ESP-IDF build of SIC_TARGET_TAB5
     * (e.g. a native/POSIX link check) from referencing an undefined
     * registration symbol. */
#   if defined(SIC_DRV_SD_SDMMC) || (defined(SIC_TARGET_TAB5) && defined(SIC_BACKEND_ESPIDF))
#       define SIC__AUTOREG_SD_SDMMC
#   endif
#   if defined(SIC_DRV_TAB5_CAM_ESPIDF) || (defined(SIC_TARGET_TAB5) && defined(SIC_BACKEND_ESPIDF))
#       define SIC__AUTOREG_TAB5_CAM_ESPIDF
#   endif
#else
    /* Opt-in mode: only register what is explicitly requested. */
#   if defined(SIC_DRV_KBD_74HC138)
#       define SIC__AUTOREG_KBD_74HC138
#   endif
#   if defined(SIC_DRV_TP4057)
#       define SIC__AUTOREG_TP4057
#   endif
#   if defined(SIC_DRV_BQ25896)
#       define SIC__AUTOREG_BQ25896
#   endif
#   if defined(SIC_DRV_KBD_TCA8418)
#       define SIC__AUTOREG_KBD_TCA8418
#   endif
#   if defined(SIC_DRV_ENCODER_GPIO)
#       define SIC__AUTOREG_ENCODER_GPIO
#   endif
#   if defined(SIC_DRV_CODEC_ES8311)
#       define SIC__AUTOREG_CODEC_ES8311
#   endif
#   if defined(SIC_DRV_MIC_PDM)
#       define SIC__AUTOREG_MIC_PDM
#   endif
#   if defined(SIC_DRV_AMP_I2S)
#       define SIC__AUTOREG_AMP_I2S
#   endif
#   if defined(SIC_DRV_IR_GPIO)
#       define SIC__AUTOREG_IR_GPIO
#   endif
#   if defined(SIC_DRV_SD_SPI)
#       define SIC__AUTOREG_SD_SPI
#   endif
#   if defined(SIC_DRV_TOUCH_GT911)
#       define SIC__AUTOREG_TOUCH_GT911
#   endif
#   if defined(SIC_DRV_TOUCH_ST712X)
#       define SIC__AUTOREG_TOUCH_ST712X
#   endif
#   if defined(SIC_DRV_RTC_RX8130)
#       define SIC__AUTOREG_RTC_RX8130
#   endif
#   if defined(SIC_DRV_IMU_BMI270)
#       define SIC__AUTOREG_IMU_BMI270
#   endif
#   if defined(SIC_DRV_TAB5_CHGCTL)
#       define SIC__AUTOREG_TAB5_CHGCTL
#   endif
#   if defined(SIC_DRV_CODEC_ES8388)
#       define SIC__AUTOREG_CODEC_ES8388
#   endif
#   if defined(SIC_DRV_CODEC_ES7210)
#       define SIC__AUTOREG_CODEC_ES7210
#   endif
#   if defined(SIC_DRV_SD_SDMMC) && defined(SIC_BACKEND_ESPIDF)
#       define SIC__AUTOREG_SD_SDMMC
#   endif
#   if defined(SIC_DRV_TAB5_CAM_ESPIDF) && defined(SIC_BACKEND_ESPIDF)
#       define SIC__AUTOREG_TAB5_CAM_ESPIDF
#   endif
#endif

/* ── Forward declarations ─────────────────────────────────────────────── */

#ifdef SIC__AUTOREG_KBD_74HC138
extern void sic_register_driver_kbd_74hc138(void);
#endif

#ifdef SIC__AUTOREG_TP4057
extern void sic_register_driver_tp4057(void);
#endif
#ifdef SIC__AUTOREG_BQ25896
extern void sic_register_driver_bq25896(void);
#endif

#ifdef SIC__AUTOREG_KBD_TCA8418
extern void sic_register_driver_kbd_tca8418(void);
#endif

#ifdef SIC__AUTOREG_ENCODER_GPIO
extern void sic_register_driver_encoder_gpio(void);
#endif

#ifdef SIC__AUTOREG_CODEC_ES8311
extern void sic_register_driver_codec_es8311(void);
#endif

#ifdef SIC__AUTOREG_MIC_PDM
extern void sic_register_driver_mic_pdm(void);
#endif

#ifdef SIC__AUTOREG_AMP_I2S
extern void sic_register_driver_amp_i2s(void);
#endif

#ifdef SIC__AUTOREG_IR_GPIO
extern void sic_register_driver_ir_gpio(void);
#endif

#ifdef SIC__AUTOREG_SD_SPI
extern void sic_register_driver_sd_spi(void);
#endif

#ifdef SIC__AUTOREG_TOUCH_GT911
extern void sic_register_driver_touch_gt911(void);
#endif

#ifdef SIC__AUTOREG_TOUCH_ST712X
/* One file (src/drivers/input/touch_st712x.c) registers two hints
 * ("touch_st7121" / "touch_st7123") via two separate functions — unlike
 * codec_es8311.c, which registers both its hints from one entry function. */
extern void sic_register_driver_touch_st7121(void);
extern void sic_register_driver_touch_st7123(void);
#endif

#ifdef SIC__AUTOREG_RTC_RX8130
extern void sic_register_driver_rtc_rx8130(void);
#endif

#ifdef SIC__AUTOREG_IMU_BMI270
extern void sic_register_driver_imu_bmi270(void);
#endif

#ifdef SIC__AUTOREG_TAB5_CHGCTL
extern void sic_register_driver_tab5_chgctl(void);
#endif

#ifdef SIC__AUTOREG_CODEC_ES8388
extern void sic_register_driver_codec_es8388(void);
#endif

#ifdef SIC__AUTOREG_CODEC_ES7210
extern void sic_register_driver_codec_es7210(void);
#endif

#ifdef SIC__AUTOREG_SD_SDMMC
extern void sic_register_driver_sd_sdmmc(void);
#endif

#ifdef SIC__AUTOREG_TAB5_CAM_ESPIDF
extern void sic_register_driver_tab5_cam_espidf(void);
#endif

/* dummy is always registered — it is the safe no-op fallback */
extern void sic_register_driver_dummy(void);

/* ── Entry point called by sic_begin_legacy ───────────────────────────── */

void sic_autoreg_drivers(void) {
#ifdef SIC__AUTOREG_KBD_74HC138
    sic_register_driver_kbd_74hc138();
#endif
#ifdef SIC__AUTOREG_TP4057
    sic_register_driver_tp4057();
#endif
#ifdef SIC__AUTOREG_BQ25896
    sic_register_driver_bq25896();
#endif
#ifdef SIC__AUTOREG_KBD_TCA8418
    sic_register_driver_kbd_tca8418();
#endif
    sic_register_driver_dummy();
#ifdef SIC__AUTOREG_ENCODER_GPIO
    sic_register_driver_encoder_gpio();
#endif
#ifdef SIC__AUTOREG_CODEC_ES8311
    sic_register_driver_codec_es8311();
#endif
#ifdef SIC__AUTOREG_MIC_PDM
    sic_register_driver_mic_pdm();
#endif
#ifdef SIC__AUTOREG_AMP_I2S
    sic_register_driver_amp_i2s();
#endif
#ifdef SIC__AUTOREG_IR_GPIO
    sic_register_driver_ir_gpio();
#endif
#ifdef SIC__AUTOREG_SD_SPI
    sic_register_driver_sd_spi();
#endif
#ifdef SIC__AUTOREG_TOUCH_GT911
    sic_register_driver_touch_gt911();
#endif
#ifdef SIC__AUTOREG_TOUCH_ST712X
    sic_register_driver_touch_st7121();
    sic_register_driver_touch_st7123();
#endif
#ifdef SIC__AUTOREG_RTC_RX8130
    sic_register_driver_rtc_rx8130();
#endif
#ifdef SIC__AUTOREG_IMU_BMI270
    sic_register_driver_imu_bmi270();
#endif
#ifdef SIC__AUTOREG_TAB5_CHGCTL
    sic_register_driver_tab5_chgctl();
#endif
#ifdef SIC__AUTOREG_CODEC_ES8388
    sic_register_driver_codec_es8388();
#endif
#ifdef SIC__AUTOREG_CODEC_ES7210
    sic_register_driver_codec_es7210();
#endif
#ifdef SIC__AUTOREG_SD_SDMMC
    sic_register_driver_sd_sdmmc();
#endif
#ifdef SIC__AUTOREG_TAB5_CAM_ESPIDF
    sic_register_driver_tab5_cam_espidf();
#endif
}
