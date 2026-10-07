# LilyGo T-Pager pins

MCU: ESP32-S3
Source: LilyGoLib hardware docs + official LILYGO wiki

## I2C bus (shared, SDA=GPIO3 SCL=GPIO2)

| Address | Device | Notes |
|---------|--------|-------|
| 0x34 | TCA8418 | Keyboard matrix controller — INT GPIO 6, KBRST via XL9555 GPIO2 |
| 0x20 | XL9555 | GPIO expander (power enables, speaker amp, etc.) |
| 0x55 | BQ27220 | Battery fuel gauge (voltage, SoC) |
| 0x6B | BQ25896 | Battery charger IC |
| 0x18 | ES8311 | Audio codec (I2S audio data via separate pins) |
| 0x28 | BHI260AP | IMU / motion sensor — INT GPIO 8 |
| 0x51 | PCF85063A | RTC — INT GPIO 1 |
| 0x5A | DRV2605 | Haptic driver |

## SPI bus (MISO=GPIO33 MOSI=GPIO34 SCK=GPIO35)

| Signal | GPIO | Notes |
|--------|------|-------|
| Display ST7796 CS | 38 | DC=37, BL=42 |
| SD Card CS | 21 | SD power via XL9555 GPIO14, detect via XL9555 GPIO12 |
| LoRa CS | 36 | RST=47, BUSY=48, IRQ=14 |
| NFC CS | 39 | ST25R3916, IRQ=5 |

## Display (SGFX build_flags)

```
-DSGFX_DRV_ST7796=1  -DSGFX_BUS_SPI=1
-DSGFX_W=480  -DSGFX_H=222  -DSGFX_ROT=1
-DSGFX_PIN_SCK=35   -DSGFX_PIN_MOSI=34  -DSGFX_PIN_MISO=-1
-DSGFX_PIN_CS=38    -DSGFX_PIN_DC=37    -DSGFX_PIN_RST=-1   -DSGFX_PIN_BL=42
-DSGFX_SPI_HZ=80000000
-DSGFX_BGR=1  -DSGFX_INVERT=1  -DSGFX_COLSTART=49  -DSGFX_ROWSTART=0  -DSGFX_MIRROR_Y=1
```

## Audio (ES8311, I2S)

| Signal | GPIO |
|--------|------|
| I2S MCLK | 10 |
| I2S SCK | 11 |
| I2S WS | 18 |
| I2S DOUT (to codec) | 45 |
| I2S DIN (from codec) | 17 |
| Speaker amp enable | XL9555 GPIO1 (I2C) |

## Other

| Signal | GPIO | Notes |
|--------|------|-------|
| Rotary A | 40 | |
| Rotary B | 41 | |
| Rotary btn | 7 | |
| GNSS TX | 12 | MIA-M10Q UART |
| GNSS RX | 4 | |
| GNSS PPS | 13 | |

## SIC build_flags (T-Pager — full peripherals)

```ini
-DI2C_SDA_PIN=3  -DI2C_SCL_PIN=2
-DTCA8418_INT_PIN=255
-DSIC_DRV_KBD_TCA8418=1
-DSIC_NO_DRV_KBD_74HC138=1
-DSIC_BATTERY_BQ27220=1
-DSIC_DRV_ENCODER_GPIO=1
-DSIC_DRV_CODEC_ES8311=1
-DES8311_MCLK_PIN=10
-DES8311_BCLK_PIN=11
-DES8311_WS_PIN=18
-DES8311_DOUT_PIN=45
-DES8311_DIN_PIN=17
-DSIC_DRV_BQ25896=1
-DSIC_DRV_SD_SPI=1
```

## Notes

- **No raw battery ADC pin** — all battery data via BQ27220 over I2C.
  `sic_battery_read()` requires `-DSIC_BATTERY_BQ27220=1`.
- **No direct keyboard GPIOs** — TCA8418 handles the full matrix scan internally.
- **Keyboard power enable**, **keyboard reset**, **speaker amp enable**, and **SD power** are routed through the XL9555 GPIO expander,
  not directly accessible as ESP32 GPIOs.
- T-Pager SD must be powered before mount. SIC's `sd_spi` config enables XL9555 GPIO14, waits for the socket to settle,
  and starts at a conservative SPI clock before faster retries.
- `tpager_keymap.c` key layout is provisional — verify each key code against hardware
  before relying on it in production.
