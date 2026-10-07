#pragma once
#ifndef SIC_NOPIN
#define SIC_NOPIN 0xFF
#endif
#ifdef __cplusplus
extern "C" {
#endif
void sic_gpio_mode(int pin, int output);
void sic_gpio_write(int pin, int val);
int  sic_gpio_read(int pin);
void sic_gpio_mode_pullup(int pin);
void sic_gpio_mode_pulldown(int pin);
#ifdef __cplusplus
}
#endif
