#if defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)
#include <Arduino.h>
#include "sic/bus/delay.h"
unsigned long sic_millis(void){ return millis(); }
void sic_delay_ms(uint32_t ms){ delay(ms); }
#endif /* SIC_BACKEND_ARDUINO */
