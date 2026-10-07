# Feature Matrix

| Area | Status | Driver / Files |
|------|--------|----------------|
| I2C bus | ✅ | `src/bus/i2c_bus_arduino.cpp` |
| GPIO bus / delay | ✅ | `src/bus/gpio_bus_arduino.c`, `src/bus/delay_arduino.c` |
| Keyboard — TCA8418 (I2C matrix, T-Pager/Cardputer-ADV) | ✅ | `src/drivers/input/kbd_tca8418_i2c.c` |
| Keyboard — 74HC138 (GPIO matrix, Cardputer/v1.1) | ✅ | `src/drivers/input/kbd_74hc138_gpio.c` |
| Keyboard event abstraction | ✅ | `src/hal/hal_kbd.c`, `include/sic/input/kscan.h` |
| Cardputer logical keymap + Fn layer | ✅ | `src/drivers/input/cardputer_keymap.c` |
| Rotary encoder — GPIO | ✅ | `src/drivers/input/encoder_gpio.c` |
| Audio codec — ES8311 (I2S mic + amp) | ✅ | `src/drivers/audio/codec_es8311.c` |
| PDM mic — SPM1423/Cardputer | ✅ | `src/drivers/audio/mic_pdm.c` |
| I2S speaker amp — NS4168/Cardputer | ✅ | `src/drivers/audio/amp_i2s.c` |
| Audio I2S backend | ✅ | `src/backends/arduino/sic_arduino_audio.cpp` |
| Battery — BQ27220 (I2C fuel gauge) | ✅ | `src/drivers/power/bq27220.c` |
| Battery — ADC (simple voltage divider) | ✅ | `src/power/battery_adc.cpp` |
| Charger — TP4057 | ⚠️ minimal | `src/drivers/power/tp4057.c` |
| Charger — BQ25896 / T-Pager | ✅ status | `src/drivers/power/bq25896.c` |
| IR TX — NEC bitbang/Cardputer | ✅ | `src/drivers/ir/ir_tx_gpio.cpp` |
| SD storage — SPI/Cardputer/T-Pager | ✅ basic mount/info | `src/drivers/storage/sd_spi_arduino.cpp` |
| Driver autoregistration | ✅ | `src/core/autoreg.c` |
| Driver registry + typed accessors | ✅ | `src/core/registry.c` |
| ESP-IDF backend — GPIO/I2C/delay/sysinfo | ✅ | `src/bus/gpio_bus_espidf.c`, `src/bus/i2c_bus_espidf.c`, `src/bus/delay_espidf.c`, `src/hal/hal_core_espidf.c` |
| ESP-IDF backend — audio (I2S STD + PDM) | ✅ | `src/backends/espidf/sic_espidf_audio.c` |
| Touch — GT911 (I2C, capacitive) | ✅ | `src/drivers/input/touch_gt911.c` |
| Touch — ST7121 / ST7123 (combo display+touch, Tab5) | ✅ | `src/drivers/input/touch_st712x.c` |
| RTC — RX8130 (I2C) | ✅ | `src/drivers/power/rtc_rx8130.c` |
| IMU — BMI270 (raw accel+gyro, no config-blob upload) | ⚠️ scoped | `src/drivers/motion/imu_bmi270.c` |
| Camera — Tab5 MIPI-CSI (`esp_video`/`esp_cam_sensor` wrapper) | ⚠️ no bounded get_frame() timeout | `src/backends/espidf/sic_espidf_camera.c` |
| SD storage — SDMMC 4-bit/ESP-IDF | ✅ basic mount/info | `src/drivers/storage/sd_sdmmc_espidf.c` |
| Audio codec — ES8388 (I2S amp/speaker, Tab5) | ⚠️ best-effort register map | `src/drivers/audio/codec_es8388.c` |
| Audio codec — ES7210 (4-ch mic ADC, Tab5) | ⚠️ 2-ch digital capture only, best-effort register map | `src/drivers/audio/codec_es7210.c` |
| Board — Tab5 (M5Stack, ESP32-P4) | ✅ | `src/boards/board_tab5.c`, `src/boards/tab5/*.c` |
