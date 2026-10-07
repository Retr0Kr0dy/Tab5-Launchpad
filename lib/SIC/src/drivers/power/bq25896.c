#include <stdint.h>
#include <string.h>
#include "sic/sic_registry.h"
#include "sic/power/charger.h"
#include "sic/power/bq25896.h"
#include "sic/bus/i2c_bus.h"

/*
 * TI BQ25896: REG0B bits [4:3] CHRG_STAT:
 *   00 = not charging / disabled
 *   01 = pre-charge
 *   10 = fast charge / constant voltage
 *   11 = charge termination done
 * REG0C exposes fault bits; non-zero means report SIC_CHG_FAULT.
 */
#define BQ25896_REG_STATUS 0x0Bu
#define BQ25896_REG_FAULT  0x0Cu

typedef struct {
    sic_bq25896_cfg_t cfg;
} bq25896_ctx_t;

static bq25896_ctx_t g_ctx;
static charger_t g_chg;

static int bq_read8(const bq25896_ctx_t* c, uint8_t reg, uint8_t* out) {
    if (!c || !out) return -1;
    return sic_i2c_writeread(c->cfg.i2c_bus, c->cfg.i2c_addr, &reg, 1, out, 1) == 1 ? 0 : -1;
}

static int bq_get_state(const void* self) {
    const bq25896_ctx_t* c = (const bq25896_ctx_t*)self;
    uint8_t status = 0, fault = 0;
    if (bq_read8(c, BQ25896_REG_STATUS, &status) != 0) return SIC_CHG_NOT_PRESENT;

    /* Reading REG0C also clears some latched fault state on this family, but
     * for a lightweight status API that is fine: report faults when present.
     */
    if (bq_read8(c, BQ25896_REG_FAULT, &fault) == 0 && fault != 0) return SIC_CHG_FAULT;

    switch ((status >> 3) & 0x03u) {
        case 0x01: /* pre-charge */
        case 0x02: /* fast charge / CV */
            return SIC_CHG_CHARGING;
        case 0x03:
            return SIC_CHG_FULL;
        default:
            return SIC_CHG_NOT_PRESENT;
    }
}

static const struct charger_vtbl_s BQ_VT = { bq_get_state };

static int probe_bq25896(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "bq25896") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_bq25896_cfg_t*)d->cfg;
    g_chg.v = &BQ_VT;
    g_chg.impl = &g_ctx;
    *out = &g_chg;
    return 0;
}

static const sic_driver_t DRV_BQ25896 = { "bq25896", SIC_F_CHARGER, probe_bq25896, NULL };

void sic_register_driver_bq25896(void) {
    sic_registry_register(&DRV_BQ25896);
}
