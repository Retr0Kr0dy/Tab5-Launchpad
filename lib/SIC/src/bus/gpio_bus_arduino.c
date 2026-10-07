#if defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)
#include <Arduino.h>
#include "sic/bus/gpio_bus.h"
void sic_gpio_mode(int pin, int output){ if (pin==0xFF) return; pinMode(pin, output? OUTPUT: INPUT); }
void sic_gpio_write(int pin, int val){ if (pin==0xFF) return; digitalWrite(pin, val? HIGH: LOW); }
int  sic_gpio_read(int pin){ if (pin==0xFF) return 1; return digitalRead(pin); }
void sic_gpio_mode_pullup(int pin){ if (pin==0xFF) return; pinMode(pin, INPUT_PULLUP); }
void sic_gpio_mode_pulldown(int pin){ if (pin==0xFF) return; pinMode(pin, INPUT_PULLDOWN); }
#endif /* SIC_BACKEND_ARDUINO */
