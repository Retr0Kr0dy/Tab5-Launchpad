/*
 * registry.c — SIC typed driver registry.
 *
 * Two separate tables:
 *  - g_drivers[]: driver descriptors registered at startup via sic_registry_register().
 *    Limited to 64 entries; drivers are statically declared so this is never a concern.
 *  - g_items[SIC_F__COUNT]: per-function singly-linked lists of probed instances.
 *    One list per sic_func_id_t (kbd, mic, amp, encoder, …). Index 0 = first device found.
 *
 * sic_begin_legacy() iterates the board IC list, matches each IC to a registered driver by
 * function ID, calls probe(), and on success appends the instance to the appropriate list.
 */

#include <stdlib.h>
#include <string.h>
#include "sic/sic.h"
#include "sic/sic_registry.h"
#include "sic/input/kscan.h"
#include "sic/audio/mic.h"
#include "sic/audio/amp.h"
#include "sic/power/switch.h"
#include "sic/power/charger.h"
#include "sic/storage/sd.h"
#include "sic/ir/ir_tx.h"
#include "sic/input/encoder.h"
#include "sic/input/touch.h"
#include "sic/motion/imu.h"
#include "sic/power/rtc.h"
#include "sic/video/camera.h"

/* Per-instance node stored in each function list. */
typedef struct item_s {
    const char*    name;
    void*          inst;
    struct item_s* next;
} item_t;

static item_t*           g_items[SIC_F__COUNT] = {0};
static const sic_driver_t* g_drivers[64];
static int                 g_ndrv = 0;

/* ── Driver table ──────────────────────────────────────────────────────────── */

void sic_registry_register(const sic_driver_t* drv) {
    if (g_ndrv < (int)(sizeof g_drivers / sizeof g_drivers[0]))
        g_drivers[g_ndrv++] = drv;
}

/* ── Instance list ─────────────────────────────────────────────────────────── */

/* Prepend a probed instance to the function list (LIFO; last registered = index 0). */
int sic_registry_add(sic_func_id_t fn, const char* name, void* inst) {
    item_t* it = (item_t*)calloc(1, sizeof *it);
    if (!it) return -1;
    it->name = name;
    it->inst = inst;
    it->next = g_items[fn];
    g_items[fn] = it;
    return 0;
}

int sic_count_fn(sic_func_id_t fn) {
    int n = 0;
    for (item_t* it = g_items[fn]; it; it = it->next) n++;
    return n;
}

const char* sic_name_fn(sic_func_id_t fn, int idx) {
    for (item_t* it = g_items[fn]; it; it = it->next) {
        if (idx-- == 0) return it->name;
    }
    return NULL;
}

void* sic_get_fn(sic_func_id_t fn, int idx) {
    for (item_t* it = g_items[fn]; it; it = it->next) {
        if (idx-- == 0) return it->inst;
    }
    return NULL;
}

/* ── Board initialisation ──────────────────────────────────────────────────── */

/* Driver registration is handled by autoreg.c (conditional on build flags). */
extern void sic_autoreg_drivers(void);

int sic_begin_legacy(const sic_board_t* board, const sic_begin_opts_t* opts) {
    (void)opts;
    sic_autoreg_drivers();

    if (!board) return 0;

    if (board->preinit) board->preinit();

    for (int i = 0; i < board->ic_count; i++) {
        const sic_board_ic_t* ic = &board->ics[i];
        for (int d = 0; d < g_ndrv; d++) {
            const sic_driver_t* drv = g_drivers[d];
            if (drv->function != ic->function) continue;
            void* inst = NULL;
            if (drv->probe(ic, &inst) == 0 && inst) {
                sic_registry_add(ic->function, drv->name, inst);
                break;  /* first matching driver wins */
            }
        }
    }
    return 1;
}

/* ── Typed accessors ───────────────────────────────────────────────────────── */

const kscan_t*   sic_kbd     (int i) { return (const kscan_t*)   sic_get_fn(SIC_F_KSCAN,   i); }
const mic_t*     sic_mic     (int i) { return (const mic_t*)     sic_get_fn(SIC_F_MIC,     i); }
const amp_t*     sic_amp     (int i) { return (const amp_t*)     sic_get_fn(SIC_F_AMP,     i); }
const pwr_sw_t*  sic_pwr_sw  (int i) { return (const pwr_sw_t*)  sic_get_fn(SIC_F_PWR_SW,  i); }
const charger_t* sic_charger (int i) { return (const charger_t*) sic_get_fn(SIC_F_CHARGER, i); }
const ir_t*      sic_ir_tx   (int i) { return (const ir_t*)      sic_get_fn(SIC_F_IR_TX,   i); }
const sd_t*      sic_sd      (int i) { return (const sd_t*)      sic_get_fn(SIC_F_SD,      i); }
const encoder_t* sic_encoder (int i) { return (const encoder_t*) sic_get_fn(SIC_F_ENCODER, i); }
const touch_t* sic_touch(int i) { return (const touch_t*) sic_get_fn(SIC_F_TOUCH, i); }
const imu_t*   sic_imu  (int i) { return (const imu_t*)   sic_get_fn(SIC_F_IMU,   i); }
const rtc_t*   sic_rtc  (int i) { return (const rtc_t*)   sic_get_fn(SIC_F_RTC,   i); }
const camera_t* sic_camera(int i) { return (const camera_t*) sic_get_fn(SIC_F_CAMERA, i); }
