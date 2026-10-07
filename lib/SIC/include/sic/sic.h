#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Driver contracts */
typedef int  (*sic_probe_fn)(const void* board_ic, void** out_inst);
typedef void (*sic_remove_fn)(void* inst);

typedef enum {
    SIC_F_KSCAN, SIC_F_MIC, SIC_F_AMP, SIC_F_PWR_SW,
    SIC_F_CHARGER, SIC_F_IR_TX, SIC_F_SD, SIC_F_ENCODER,
    SIC_F_TOUCH, SIC_F_IMU, SIC_F_RTC, SIC_F_CAMERA,
    SIC_F__COUNT
} sic_func_id_t;

const char*   sic_func_name(sic_func_id_t);
sic_func_id_t sic_func_id_from_name(const char*);

/* Registry-visible driver descriptor */
typedef struct sic_driver_s {
    const char*   name;
    sic_func_id_t function;
    sic_probe_fn  probe;
    sic_remove_fn remove;
} sic_driver_t;

/* Board description */
typedef struct sic_board_ic_s {
    sic_func_id_t function;
    const char*   hint;
    const void*   cfg;
} sic_board_ic_t;

typedef struct sic_board_s {
    const char*            name;
    const sic_board_ic_t*  ics;
    int                    ic_count;
    void                 (*preinit)(void); /* called before probing: release reset lines, init GPIO expanders, etc. */
} sic_board_t;

typedef enum {
    SIC_OK=0, SIC_ENOENT=-2, SIC_EINVAL=-3, SIC_EIO=-5, SIC_ENOMEM=-12
} sic_err_t;

#ifndef SIC_NOPIN
#define SIC_NOPIN 0xFF
#endif

#ifdef __cplusplus
}
#endif

/* Backend selection (auto-detects ARDUINO if no explicit backend chosen) */
#include "sic/sic_backend.h"

/* Public sub-headers — all use sic/ prefix for include-path independence */
#include "sic/hal.h"
#include "sic/audio.h"
#include "sic/audio_compat.h"
#include "sic/audio/mic.h"
#include "sic/audio/amp.h"
#include "sic/input/kscan.h"
#include "sic/input/encoder.h"
#include "sic/input/touch.h"
#include "sic/power/battery.h"
#include "sic/power/charger.h"
#include "sic/power/rtc.h"
#include "sic/motion/imu.h"
#include "sic/video/camera.h"
#include "sic/sic_board.h"
#include "sic/sic_registry.h"
