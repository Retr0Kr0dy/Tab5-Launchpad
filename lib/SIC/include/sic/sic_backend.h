#pragma once
/*
 * sic_backend.h — Platform backend selection and contract.
 *
 * SIC separates portable logic (core, drivers, registry) from platform I/O
 * primitives (GPIO, I2C, delay, sysinfo). Each backend supplies a fixed set
 * of link-time symbols; see the CONTRACT section below.
 *
 * ── Available backends ────────────────────────────────────────────────────
 *
 *   SIC_BACKEND_ARDUINO   Arduino framework (ESP32, STM32-Arduino, RP2040…)
 *                         Sources compiled in:
 *                           src/bus/gpio_bus_arduino.c
 *                           src/bus/i2c_bus_arduino.cpp
 *                           src/bus/delay_arduino.c
 *                           src/hal/hal_core.cpp        (sic_sysinfo)
 *                           src/backends/arduino/sic_arduino_audio.cpp
 *                           src/power/battery_adc.cpp
 *
 *   SIC_BACKEND_ESPIDF    ESP-IDF native, v5.x (no Arduino layer)
 *                         Sources compiled in:
 *                           src/bus/gpio_bus_espidf.c
 *                           src/bus/i2c_bus_espidf.c    (driver/i2c_master.h)
 *                           src/bus/delay_espidf.c
 *                           src/hal/hal_core_espidf.c   (sic_sysinfo)
 *
 *   SIC_BACKEND_POSIX     POSIX / host-side unit tests — stub implementations
 *                         expected in src/backends/posix/.
 *
 * ── Backend contract ──────────────────────────────────────────────────────
 *
 *   Every backend must provide the following C symbols:
 *
 *   GPIO  (sic/bus/gpio_bus.h):
 *     void sic_gpio_mode(int pin, int output);
 *     void sic_gpio_write(int pin, int val);
 *     int  sic_gpio_read(int pin);
 *     void sic_gpio_mode_pullup(int pin);
 *     void sic_gpio_mode_pulldown(int pin);
 *
 *   Timing  (sic/bus/delay.h):
 *     void          sic_delay_ms(uint32_t ms);
 *     unsigned long sic_millis(void);
 *
 *   I2C bus  (sic/bus/i2c_bus.h):
 *     int sic_i2c_begin_bus(int bus, int sda, int scl, uint32_t hz);
 *     int sic_i2c_scan_bus(int bus, uint8_t* addrs, int max);
 *     int sic_i2c_write(int bus, uint8_t addr, const uint8_t* buf, int n);
 *     int sic_i2c_read(int bus, uint8_t addr, uint8_t* buf, int n);
 *     int sic_i2c_writeread(int bus, uint8_t addr,
 *                           const uint8_t* wr, int nw,
 *                           uint8_t* rd,       int nr);
 *
 *   System info  (sic/hal.h):
 *     int sic_sysinfo(sic_sysinfo_t* out);
 *
 * ── Auto-selection ────────────────────────────────────────────────────────
 *
 * If no SIC_BACKEND_* is explicitly defined, the Arduino backend is selected
 * when the ARDUINO macro is present (PlatformIO / Arduino IDE always define
 * it). Override by defining SIC_BACKEND_ESPIDF or SIC_BACKEND_POSIX before
 * including any SIC header.
 */
#if !defined(SIC_BACKEND_ARDUINO) && \
    !defined(SIC_BACKEND_ESPIDF)  && \
    !defined(SIC_BACKEND_POSIX)
#  if defined(ARDUINO)
#    define SIC_BACKEND_ARDUINO
#  endif
#endif
