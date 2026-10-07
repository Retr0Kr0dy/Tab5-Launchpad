
extern "C" {
#include "sic/sic.h"
#include "sic/power/battery.h"
}

#ifndef BAT_ADC_PIN
#  if !defined(SIC_BATTERY_BQ27220) && !defined(SIC_BATTERY_INA226)
/* No ADC pin and no I2C gauge selected — stub returns SIC_ENOENT. */
int sic_battery_read(sic_battery_t* out){
  (void)out;
  return SIC_ENOENT;
}
#  endif /* SIC_BATTERY_BQ27220 || SIC_BATTERY_INA226 */
#else

#if defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)
#include <Arduino.h>

#ifndef BAT_DIV_K
#define BAT_DIV_K 2.0f
#endif

static int percent_from_voltage(float v){
  // crude mapping 3.3V=0%, 4.2V=100%
  if (v <= 3.30f) return 0;
  if (v >= 4.20f) return 100;
  return (int)((v - 3.30f) * (100.0f / (4.20f - 3.30f)));
}

int sic_battery_read(sic_battery_t* out){
  if (!out) return SIC_EINVAL;
  analogReadResolution(12);
  float mv = (float)analogRead(BAT_ADC_PIN) * (3300.0f/4095.0f) * BAT_DIV_K;
  out->voltage_v = mv / 1000.0f;
  out->percent = percent_from_voltage(out->voltage_v);
  out->current_ma = 0.0f;   /* ADC-only path has no current sense */
  return 0;
}
#endif /* SIC_BACKEND_ARDUINO */

#endif /* BAT_ADC_PIN */
