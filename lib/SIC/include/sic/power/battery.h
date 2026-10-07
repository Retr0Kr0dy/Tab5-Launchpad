#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* current_ma: signed, positive = charging (current flowing into the pack),
 * negative = discharging. 0 (exactly) on drivers that have no current sense
 * at all -- callers that need to distinguish "no current sense" from
 * "genuinely zero current" should treat near-zero within a few mA as noise
 * either way, not act on the sign alone. */
typedef struct { float voltage_v; int percent; float current_ma; } sic_battery_t;
int sic_battery_read(sic_battery_t* out);
#ifdef __cplusplus
}
#endif
