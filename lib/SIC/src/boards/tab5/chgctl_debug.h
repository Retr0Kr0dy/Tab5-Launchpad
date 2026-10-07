/*
 * chgctl_debug.h — Tab5-private visibility into the IP2326 charge-status
 * driver's debounce/fallback state for the live power diagnostic screen.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* out_raw_charging: most recent CHG_STAT/fallback charging candidate (0/1)
 * before debounce.
 * out_confirmed: debounced SIC_CHG_* state returned to callers.
 * out_last_current_ma: most recent normalized INA226 current used when the
 * CHG_STAT expander read failed (positive=charging); otherwise the previous
 * fallback value.
 * out_used_current_fallback: 1 if the latest sample used INA226 because the
 * CHG_STAT read failed, 0 if the real IP2326 status pin was read.
 */
void tab5_chgctl_debug_get(int* out_raw_charging,
                           int* out_confirmed,
                           float* out_last_current_ma,
                           int* out_used_current_fallback);

#ifdef __cplusplus
}
#endif
