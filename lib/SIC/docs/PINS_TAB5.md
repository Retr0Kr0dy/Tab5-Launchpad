# M5Stack Tab5 pins

MCU: ESP32-P4 (application core) + ESP32-C6 co-processor (Wi-Fi/BT over SDIO, no radio on the P4 itself)
Source: mined from M5Stack's official ESP-IDF reference firmware
(`M5Tab5-UserDemo`), cross-checked against this repo's own committed Tab5 sources where noted below.

## I2C bus 0 — internal (SDA=GPIO31, SCL=GPIO32, 400kHz)

Owned by `boards/tab5/ioexpander.c` (`tab5_ioexp_init()`, called first in `board_tab5.c`'s `preinit()`);
every other internal-bus driver assumes that call already ran.

| Address | Device | Notes |
|---------|--------|-------|
| 0x43 | PI4IOE5V6408 expander 0 | HP_DET, CAM_RST, TP_RST, LCD_RST, EXT5V_EN, SPK_EN, RF antenna select — bit map below |
| 0x44 | PI4IOE5V6408 expander 1 | CHG_EN, CHG_STAT, nCHG_QC_EN, PWROFF_PLUSE, USB5V_EN, WLAN_PWR_EN — bit map below |
| 0x32 | RX8130 | RTC — `SIC_F_RTC`, hint `rx8130` |
| 0x68 | BMI270 | IMU (accel+gyro) — `SIC_F_IMU`, hint `bmi270` |
| 0x14 | GT911 (alt address) | Touch, only present on ILI9881C+GT911 panel variant — `SIC_F_TOUCH`, hint `touch_gt911` |
| 0x55 | ST7121 / ST7123 | Combined display+touch identity register AND touch controller — `SIC_F_TOUCH`, hints `touch_st7121`/`touch_st7123` |
| 0x40 | ES7210 | 4-ch mic ADC — `SIC_F_MIC`, hint `codec_es7210` |
| 0x10 | ES8388 | Speaker/DAC codec — `SIC_F_AMP`, hint `codec_es8388` |
| — | SCCB (camera sensor config) | Rides this same bus — `esp_video`'s CSI config borrows the live I2C bus 0 handle (`sic_espidf_camera.c`) rather than opening a second master |

Panel/touch identity is a single detect step (`boards/tab5/panel_detect.c`, `tab5_panel_detect()`),
run once by board `preinit()` after `tab5_ioexp_init()` and the explicit LCD/touch reset pulse:
probe 0x14 (GT911) first, else
probe 0x55 and read its 1-byte FW-version register at 0x0000 (value 1 = ST7121, value 3 = ST7123,
anything else/read failure = ST7123 fallback), else fall back to ILI9881C+GT911 if neither ACKs at all.
The result is cached (`tab5_panel_detected()`) for the 3 touch driver `probe()`s to self-select against
— see `src/boards/board_tab5.c`'s file header for why all 3 touch hints are declared unconditionally
rather than the board file picking one at compile time.

## I2C bus 1 — Grove / Port A (SCL=GPIO54, SDA=GPIO53)

Independent bus, not initialised by any Tab5-private module — free for application/Grove peripherals.

## GPIO expander bit maps (PI4IOE5V6408 pair)

Authoritative source: `boards/tab5/ioexpander.c`'s own comments and boot-time `OUT_SET`/`IO_DIR` values
(Phase 3, transcribed here verbatim — that file is the in-tree source of truth, not this doc).

### Expander 0 (0x43) — `IO_DIR=0x7F`, `OUT_SET=0x76` (`0b01110110`) at boot

| Bit | Signal | Direction | Boot level |
|-----|--------|-----------|------------|
| 7 | HP_DET (headphone detect) | input | — |
| 6 | CAM_RST | output | 1 (deasserted) |
| 5 | TP_RST | output | 1 (deasserted) |
| 4 | LCD_RST | output | 1 (deasserted) |
| 3 | NC | — | — |
| 2 | EXT5V_EN | output | 1 (enabled) |
| 1 | SPK_EN | output | 1 (enabled) |
| 0 | RF antenna select (0=internal, 1=external) | output | 0 (internal) |

### Expander 1 (0x44) — `IO_DIR=0xB9`, `OUT_SET=0x89` (`0b10001001`) at boot

| Bit | Signal | Direction | Boot level |
|-----|--------|-----------|------------|
| 7 | CHG_EN | output | 1 (enabled by Orion) |
| 6 | CHG_STAT (charge status) | input | — (high while actively charging) |
| 5 | nCHG_QC_EN | output | 0 |
| 4 | PWROFF_PLUSE | output | 0 |
| 3 | USB5V_EN | output | 1 (enabled) |
| 2-1 | NC | — | — |
| 0 | WLAN_PWR_EN | output | 1 (enabled) |

## Touch INT / reset

| Signal | GPIO | Notes |
|--------|------|-------|
| Touch INT | 23 | Shared by GT911 and ST7121/ST7123 (all report via this line; SIC's touch drivers poll rather than use it, `pin_int` is carried in `sic_touch_cfg_t` for future interrupt-driven use) |
| TP_RST / LCD_RST | expander 0, bits 5/4 | Shared reset line between touch and display — released once by `tab5_ioexp_init()`, must run before both display bring-up and touch/panel detection |

## Audio — I2S0 (shared by ES8388 + ES7210)

| Signal | GPIO |
|--------|------|
| MCLK | 30 |
| BCLK | 27 |
| WS (LRCK) | 29 |
| DOUT (ESP32 -> ES8388 DAC) | 26 |
| DIN (ES7210 ADC -> ESP32) | 28 |

Both chips are I2S *slaves* (the ESP32-P4 side always opens `I2S_ROLE_MASTER` —
`sic_espidf_audio.c`). See `src/drivers/audio/codec_es7210.c`'s file header for the current
4-channel-capture path (chip analog front end configured for all 4 mics, delivered over an
independent RX-only TDM I2S channel, not the shared duplex pair `sic_codec_open()` uses for TX).

## SD card — 4-bit SDMMC

| Signal | GPIO |
|--------|------|
| CLK | 43 |
| CMD | 44 |
| D0 | 39 |
| D1 | 40 |
| D2 | 41 |
| D3 | 42 |

Powered from on-chip LDO channel 4 @ 3300mV (`sic_sd_sdmmc_cfg_t{ldo_chan=4, ldo_mv=3300}`,
`src/drivers/storage/sd_sdmmc_espidf.c`).

## Camera — MIPI-CSI + LEDC clock

| Signal | GPIO | Notes |
|--------|------|-------|
| XCLK (LEDC-driven) | 36 | 24MHz, 1-bit duty resolution (the only resolution that produces exactly 24MHz from ESP32-P4's 80MHz LEDC source — see `sic_espidf_camera.c`'s comment) |
| CAM_RST | expander 0, bit 6 | Driven by the camera driver itself around `start()`, not a raw GPIO |
| SCCB (sensor config) | I2C bus 0 | Shared bus handle, `init_sccb=false` passed to `esp_video` |

Camera clock uses `LEDC_TIMER_1`, deliberately *not* `LEDC_TIMER_0` — the reference firmware's own
`bsp_cam_osc_init()`/`bsp_display_brightness_init()` both claim `LEDC_TIMER_0` and silently stomp each
other; SIC's camera driver leaves `LEDC_TIMER_0` free for a future Tab5 backlight implementation.
5 supported sensors, auto-probed by `esp_video_init()`: sc2336, sc202cs, sc101iot, sc035hgs, sc030iot
(exactly one must be enabled in sdkconfig — a firmware-config concern, not a SIC driver one).

## Display — MIPI-DSI, 720x1280 portrait, 2 data lanes

DSI PHY power: on-chip LDO channel 3 @ 2500mV (`SGFX_DSI_LDO_CHAN`/`SGFX_DSI_LDO_MV`,
`include/sgfx_hal.h`/`sgfx_port.h` in the sibling SGFX repo — SGFX owns the display bus, SIC does not;
this section is transcribed here because the rough lane-rate/clock figures quoted elsewhere
differ from what SGFX's committed Tab5 panel drivers actually use).

SGFX's own driver file headers document these as the
literal values the real Tab5 board bring-up code (`m5stack_tab5.c`) runs with, not the generic
per-panel component headers' template defaults:

| Panel | Lane rate | DPI clock | H (hsync/hbp/hfp) | V (vsync/vbp/vfp) |
|-------|-----------|-----------|--------------------|---------------------|
| ILI9881C | 730 Mbps/lane | 60 MHz | 140/40/40 (`src/drivers/ili9881c.c`'s own header states this triple as documented; field-by-field mapping not independently re-derived from that file's prose — cross-check exact `hsync`/`hbp`/`hfp` split against the driver source directly before relying on it) | 20/4/20 |
| ST7121 | 965 Mbps/lane | 70 MHz | hsync=2, hbp=40, hfp=40 | vsync=20, vbp=24, vfp=200 |
| ST7123 | 965 Mbps/lane (shared config w/ ST7121) | 70 MHz | hsync=2, hbp=40, hfp=40 (shared w/ ST7121) | vsync=8, vbp=2, vfp=220 |

These **differ from the rough hardware-inventory figures quoted elsewhere** (ST7121 as "900 Mbps"
and no recorded ILI9881C/ST7123 lane rates) — SGFX's driver source is authoritative. Note also:
`include/sgfx_hal.h`'s field comment for `lane_mbps` still says "Tab5: 900 or 1040 depending on panel" —
that comment is stale; the per-panel driver file headers (this table's source) are the current
ground truth, not that struct comment.

Backlight: PWM on GPIO22 (`LEDC_TIMER_0`, left free by the camera driver's `LEDC_TIMER_1` choice — see
Camera section above).

## Wi-Fi/BT — ESP32-C6 co-processor over SDIO (esp-hosted)

| Signal | GPIO / bit |
|--------|------------|
| SDIO CLK | 12 |
| SDIO CMD | 13 |
| SDIO D0 | 11 |
| SDIO D1 | 10 |
| SDIO D2 | 9 |
| SDIO D3 | 8 |
| C6 RST | 15 (direct SoC GPIO, not an expander bit) |
| WLAN_PWR_EN | expander 1, bit 0 |
| RF antenna select | expander 0, bit 0 (0=internal, 1=external) |

SIC only owns power-sequencing (`boards/tab5/wifi_power.c`, `tab5_wifi_power_init()`, called last in
`board_tab5.c`'s `preinit()`): rail on, settle, release C6 reset. The SDIO data pins themselves and the
esp-hosted protocol stack are consumed directly by app-layer code instead — no SIC function ID
for Wi-Fi/BT. The C6 also needs a one-time SDIO slave
firmware flash before any of this works — see `docs/BRINGUP_TAB5.md`'s preamble step.

## Grove ports

| Port | Signals | Notes |
|------|---------|-------|
| Port A | I2C, GPIO54 (SCL) / GPIO53 (SDA) | Same bus as "I2C bus 1" above |
| Port B | GPIO17 / GPIO52 | |
| Port C | UART, GPIO7 (TX?) / GPIO6 (RX?) | Not yet wired to any SIC driver |

## Other

| Signal | GPIO | Notes |
|--------|------|-------|
| M-Bus / M5Module* | 30-pin bottom connector | Out of scope for this bring-up |

## SIC build_flags (Tab5 — full peripherals)

```ini
-DSIC_BACKEND_ESPIDF=1
-DSIC_TARGET_TAB5=1
```

`SIC_TARGET_TAB5` alone auto-enables every pure-C99 Tab5 driver via `src/core/autoreg.c`'s opt-out
convention (touch x3, RTC, IMU, charger, ES8388, ES7210); the two ESP-IDF-only drivers (SD-SDMMC,
camera) additionally require `SIC_BACKEND_ESPIDF` to be defined (always true for a real Tab5 firmware
build — see `autoreg.c`'s comment on why that extra gate exists: a non-ESP-IDF `SIC_TARGET_TAB5` build,
e.g. a native link check, must not reference those two drivers' registration symbols).

## Notes

- **No physical buttons/LEDs on Tab5 at all.** Unlike T-Pager/Cardputer, there is nothing here for
  `SIC_F_KSCAN`/`SIC_F_ENCODER` to bind to — M5Unified emulates BtnA/B/C from the bottom touch strip at
  the application layer, not something SIC's board descriptor owns.
- **Charging:** an IP2326 charger is controlled through expander 1. `CHG_EN` is bit 7,
  `nCHG_QC_EN` is bit 5, and `CHG_STAT` is bit 6 (high while actively charging). The current SIC
  charger API intentionally exposes only `SIC_CHG_CHARGING` versus `SIC_CHG_NOT_PRESENT`/not actively
  charging because this board-level status line does not distinguish disconnected, charge-complete,
  or charger fault states.
- **Battery monitor:** INA226 @ 0x41 supplies voltage/current telemetry. Its raw shunt-current sign is
  opposite SIC's public convention on Tab5, so `battery_ina226.c` normalizes it to positive=charging,
  negative=discharging.
- Several driver-level assumptions (touch record stride, RTC WDAY encoding, BMI270
  config-blob omission, ES8388/ES7210 register-level confidence) are flagged in each driver's own file
  header — this pins doc is the wiring reference, not a restatement of those caveats.
