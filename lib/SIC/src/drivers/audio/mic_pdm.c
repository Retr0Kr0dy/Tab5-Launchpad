#define SIC_AUDIO_NO_COMPAT 1
/*
 * mic_pdm.c — typed SIC mic_t wrapper around the low-level PDM I2S backend.
 *
 * This keeps applications/examples target-agnostic: a board exposes SIC_F_MIC
 * with hint "mic_pdm", and users call sic_mic(0)->v->start/read without
 * knowing about SPM1423 pins, slots, or I2S mode.
 */

#include <string.h>
#include "sic/sic.h"
#include "sic/audio.h"
#include "sic/audio/mic.h"
#include "sic/audio/mic_pdm.h"
#include "sic/sic_registry.h"

typedef struct {
    sic_pdm_mic_cfg_t cfg;
    int sample_rate;
    int open;
} pdm_ctx_t;

static pdm_ctx_t g_ctx;
static mic_t g_mic;

static int pdm_start(const void* self, int sample_rate_hz) {
    pdm_ctx_t* c = (pdm_ctx_t*)((const mic_t*)self)->impl;
    if (!c) return -1;
    if (sample_rate_hz <= 0) sample_rate_hz = 16000;

    /* Re-open on every start().  The low-level audio backend is shared with
     * the speaker helper, so another audio path may have closed the I2S mic
     * since this typed driver was last used.  start() is cheap and makes the
     * typed abstraction robust against those backend switches.
     */
    if (c->open) c->open = 0;
    sic_mic_close();

    int rc = sic_mic_open(c->cfg.clk_pin, c->cfg.data_pin,
                          sample_rate_hz, c->cfg.right_slot);
    if (rc != 0) return rc;
    c->sample_rate = sample_rate_hz;
    c->open = 1;
    return 0;
}

static int pdm_read(const void* self, short* buf, int n) {
    pdm_ctx_t* c = (pdm_ctx_t*)((const mic_t*)self)->impl;
    if (!c || !c->open || !buf || n <= 0) return 0;
    return sic_mic_read((int16_t*)buf, (size_t)n, 200);
}

static const struct mic_vtbl_s PDM_MIC_VT = { pdm_start, pdm_read };

static int probe_pdm_mic(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "mic_pdm") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_pdm_mic_cfg_t*)d->cfg;
    g_ctx.sample_rate = 16000;
    g_mic.v = &PDM_MIC_VT;
    g_mic.impl = &g_ctx;
    *out = &g_mic;
    return 0;
}

static const sic_driver_t DRV_PDM_MIC = {
    "mic_pdm", SIC_F_MIC, probe_pdm_mic, NULL
};

void sic_register_driver_mic_pdm(void) {
    sic_registry_register(&DRV_PDM_MIC);
}
