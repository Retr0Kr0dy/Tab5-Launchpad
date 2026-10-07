# Tab5 Board Bring-Up

Same terse, serial-log-driven format as `docs/COOKBOOK_BOARD_BRINGUP.md`, following an
earliest-failure-isolation order for this board. Each numbered step is a real hardware milestone
to check off with a scope/logic analyzer or serial log before moving to the next — don't chase
step 6 while step 2 is still silently failing.

## Preamble — one-time, before anything below (not a SIC driver's job)

**Flash the ESP32-C6's SDIO slave firmware.** The onboard ESP32-C6 co-processor (Wi-Fi/BT — the
ESP32-P4 has no radio of its own) ships needing a one-time signed binary blob flashed to it before the
SDIO link to it means anything at all. This is a **provisioning step done once per board**, via the
reference repo's `platforms/tab5/wifi_c6_fw/flash.sh` — no source is available for this blob, it is
flashed as-is. It is *not* something any SIC driver does at runtime: `boards/tab5/wifi_power.c`
(`tab5_wifi_power_init()`) only powers the rail and releases the C6's reset line; it assumes the C6
already has working firmware on it. Skip this step and every symptom from here on downstream of Wi-Fi/
BT will look like a hardware fault instead of an unflashed co-processor.

## 1. UART console

**Checking for:** the ESP32-P4 boots and its console UART is alive at all, before any peripheral is
touched. If this fails, nothing below is reachable — check the board's power-on sequencing and USB/UART
bridge before anything else.

## 2. I2C bus 0 + both GPIO expanders

**Checking for:** `tab5_ioexp_init()` (`boards/tab5/ioexpander.c`, called first in `board_tab5.c`'s
`preinit()`) can open I2C bus 0 (SDA=31, SCL=32) and both PI4IOE5V6408 expanders (0x43, 0x44) ACK their
register writes. This is the single most load-bearing step on this board: LCD_RST, TP_RST, CAM_RST,
EXT5V_EN, SPK_EN, WLAN_PWR_EN, USB5V_EN, CHG_EN all come from these two chips — every step below this
one depends on it having actually run. A bus scan should show 0x43 and 0x44 responding before
proceeding; if either is silent, check the internal I2C bus wiring before suspecting any downstream
chip.

## 3. RTC / IMU (basic I2C sanity)

**Checking for:** RX8130 (0x32) and BMI270 (0x68) both ACK on the now-confirmed-good I2C bus 0 — a
cheap two-chip sanity check on the bus itself before moving to the trickier shared-reset-line devices
(touch/display) below. `sic_rtc(0)->v->get_time()` returning plausible (even if not yet correctly
time-set) BCD-decoded fields, and `sic_imu(0)->v->read()` returning a live-updating chip ID of 0x24
internally (not exposed directly, but a persistent `imu_read()` failure means BMI270 isn't answering),
both confirm the bus is healthy beyond just the expanders.

## 4. Display DSI

**Checking for:** the MIPI-DSI panel lights up at all. Orion's `main/display.c` asks SIC for the
cached panel identity, creates the matching DSI bus with `sgfx_hal_make_dsi()`, then calls `sgfx_init()`
with the selected panel ops table. SIC board preinit must therefore complete first: it opens I2C,
configures the expanders, pulses LCD/touch reset, and caches `tab5_panel_detected()` before DSI setup.
If the panel stays dark, confirm the cached panel identity and reset sequence before suspecting DSI
lane-rate/timing values.

## 5. Touch

**Checking for:** exactly one of the three touch drivers (`touch_gt911`, `touch_st7121`, `touch_st7123`
— all three always registered, see `src/boards/board_tab5.c`'s file header for why) is producing real
touch points, and it's the one matching the panel that just lit up in step 4. `sic_count_cap(SIC_CAP_
TOUCH)` should read 1 on an ILI9881C+GT911 unit, or 2 on an ST7121/ST7123 unit (the spurious-but-harmless
GT911 entry that never gets real hardware to answer it — see the board file's comment on why `sic_touch
(0)` is still guaranteed to be the correct device either way). If touch points look plausible but
inverted/rotated relative to the lit panel, that's an SGFX-side coordinate-mapping issue, not this
driver.

## 6. Grove I2C bus 1

**Checking for:** the Port A / Grove I2C bus (SCL=54, SDA=53) is independently alive — it shares no
wiring with bus 0's expanders, so this is a clean check that doesn't depend on any of the steps above.
Useful mainly as a known-good bus to plug a known peripheral into while validating a new Grove
accessory, not something Tab5 itself depends on.

## 7. Audio

**Checking for:** ES8388 (0x10, `codec_es8388`) and ES7210 (0x40, `codec_es7210`) both ACK on I2C, and
`sic_amp(0)->v->beep_ms()` produces an audible tone. Both codecs' register maps carry the lowest
confidence of any Tab5 driver (see each file's header) — if the beep is silent or distorted, that's the
first place to check with a scope on the shared I2S0 lines (MCLK=30, BCLK=27, WS=29) before assuming a
wiring fault. ES7210 capture uses the proven 48kHz, 4-slot TDM path and returns raw interleaved
MIC1..MIC4 samples through `sic_mic(0)->v->read()`; see `codec_es7210.c` for the shared-pin handoff with
ES8388 and the register-confidence notes.

## 8. SD

**Checking for:** `sic_sd_present()` mounts the card over the 4-bit SDMMC bus (CLK=43, CMD=44, D0-D3=
39-42), powered from on-chip LDO channel 4 @ 3300mV. A mount failure with a card known to be good is
usually the LDO channel/voltage, not the data pins — confirm LDO channel 4 is actually the one wired to
the SD IO rail on this exact board revision before re-checking pin assignments.

## 9. USB host

**Checking for:** USB5V_EN / EXT5V_EN rails (both expander-gated) come up and a directly attached
USB-A device enumerates through the app-layer host stack. Orion does not infer USB-A/USB-C cable
presence from expander 1 bit 6: that input is the IP2326 `CHG_STAT` line, not a generic cable detector.
USB host support lives in the app layer instead of SIC (see the app-layer USB HID host code).

## 10. Charger / power status

**Checking for:** `sic_charger_state()` (`tab5_chgctl`) reports `SIC_CHG_CHARGING` while the IP2326
asserts expander 1 bit 6 / `CHG_STAT`. Expander bit 7 / `CHG_EN` is driven high by board initialization.
Cross-check the status transition against INA226 current telemetry while connecting external power.
The board-level status signal does not distinguish disconnected, charge-complete, or charger-fault
states, so SIC deliberately does not synthesize `SIC_CHG_FULL`/`SIC_CHG_FAULT` from it.

## 11. Camera

**Checking for:** whichever of the 5 supported sensors (sc2336, sc202cs, sc101iot, sc035hgs, sc030iot)
is physically fitted gets auto-detected by `esp_video_init()`, XCLK (GPIO36, LEDC_TIMER_1) is running at
24MHz, and `sic_camera(0)->v->start()` + `get_frame()` return real frame data. Left until near the end
of this order deliberately: it depends on I2C bus 0 (step 2) and pulls in the heaviest third-party
component of this whole bring-up (`esp_video`/`esp_cam_sensor`), so isolate every earlier, simpler
failure mode first. Remember `get_frame()`'s `timeout_ms` is currently a no-op (blocks until a frame
arrives) — a capture that never returns needs its own task, not a shorter timeout.

## 12. ESP32-C6 Wi-Fi/BT

**Checking for:** after the preamble's one-time SDIO firmware flash, `tab5_wifi_power_init()` (rail on,
settle 20ms, release GPIO15 reset, 50ms boot settle) results in the C6 becoming enumerable over SDIO
(CLK=12, CMD=13, D0-D3=11/10/9/8) when Orion lazily starts `esp_hosted`. SIC's responsibility ends at
"the C6 is powered and out of reset"; if the SDIO transport fails after that, investigate Orion's
esp-hosted/C6 provisioning path unless the rail/reset sequence itself measures wrong on a scope.

## 13. Poweroff sequence (last, destructive)

**Checking for:** `PWROFF_PLUSE` (expander 1, bit 4) actually powers the board down when pulsed. Tested
last and deliberately — every earlier step should already be validated, since this one ends the debug
session by design.
