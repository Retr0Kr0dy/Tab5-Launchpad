/*
 * charger_tab5_ioexp.c — M5Stack Tab5 IP2326 charger status.
 *
 * The Tab5 has a dedicated IP2326 charger. Its control/status lines are
 * routed through PI4IOE5V6408 expander 1 (0x44):
 *   bit 7 CHG_EN      output, active high
 *   bit 5 nCHG_QC_EN  output, active low
 *   bit 6 CHG_STAT    input, high while charging
 *
 * tab5_ioexp_init() enables CHG_EN at boot. This SIC_F_CHARGER driver reads
 * the real CHG_STAT line and debounces it with a two-consecutive-sample
 * rule. If that expander read fails, it falls back to INA226 pack current;
 * sic_battery_read() normalizes its sign to the SIC convention
 * positive=charging, negative=discharging.
 *
 * CHG_STAT is a charging-status signal, not a fuel-gauge interface. A low
 * level can therefore mean no external power, charge termination/full, or
 * simply not actively charging. SIC_CHG_FULL and SIC_CHG_FAULT cannot be
 * asserted reliably from the exposed signal set, so this driver reports
 * only SIC_CHG_CHARGING versus SIC_CHG_NOT_PRESENT (the latter should be
 * read as "not actively charging" on Tab5).
 *
 * Board-private expander plumbing remains below SIC's public API; probe()
 * performs no I/O. Pure C99, no ESP-IDF headers.
 */
#include <string.h>
#include "sic/sic_registry.h"
#include "sic/power/charger.h"
#include "sic/power/battery.h"
#include "boards/tab5/ioexpander.h"
#include "boards/tab5/chgctl_debug.h"

#define TAB5_CHG_FALLBACK_FLOOR_MA 20.0f

static charger_t g_chg;
static int s_last_raw = -1;             /* 0/1 charging candidate before debounce */
static int s_confirmed = SIC_CHG_NOT_PRESENT;
static float s_last_current_ma = 0.0f;
static int s_used_current_fallback = 0;

static int chg_get_state(const void* self) {
    (void)self;

    int raw = tab5_ioexp_get_in(1, 6);  /* IP2326 CHG_STAT */
    s_used_current_fallback = 0;

    if (raw < 0) {
        sic_battery_t bat;
        if (sic_battery_read(&bat) != 0) return s_confirmed;
        s_last_current_ma = bat.current_ma;
        raw = bat.current_ma > TAB5_CHG_FALLBACK_FLOOR_MA ? 1 : 0;
        s_used_current_fallback = 1;
    }

    raw = raw ? 1 : 0;
    if (raw == s_last_raw) {
        s_confirmed = raw ? SIC_CHG_CHARGING : SIC_CHG_NOT_PRESENT;
    }
    s_last_raw = raw;
    return s_confirmed;
}

void tab5_chgctl_debug_get(int* out_raw_charging,
                           int* out_confirmed,
                           float* out_last_current_ma,
                           int* out_used_current_fallback) {
    if (out_raw_charging) *out_raw_charging = s_last_raw;
    if (out_confirmed) *out_confirmed = s_confirmed;
    if (out_last_current_ma) *out_last_current_ma = s_last_current_ma;
    if (out_used_current_fallback) *out_used_current_fallback = s_used_current_fallback;
}

static const struct charger_vtbl_s TAB5_CHGCTL_VT = { .get_state = chg_get_state };

static int probe_tab5_chgctl(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "tab5_chgctl") != 0) return -1;
    g_chg.v = &TAB5_CHGCTL_VT;
    g_chg.impl = NULL;
    *out = &g_chg;
    return 0;
}

static const sic_driver_t DRV_TAB5_CHGCTL = {
    "tab5_chgctl", SIC_F_CHARGER, probe_tab5_chgctl, NULL
};

void sic_register_driver_tab5_chgctl(void) {
    sic_registry_register(&DRV_TAB5_CHGCTL);
}
