# Troubleshooting

**No serial logs on Cardputer / ESP32-S3** — enable USB CDC in the PlatformIO env. Upload can work while `Serial.print()` goes to an unseen UART if these flags are missing:

```ini
monitor_speed = 115200
monitor_dtr = 0
monitor_rts = 0
build_flags =
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MSC_ON_BOOT=0
  -DARDUINO_USB_DFU_ON_BOOT=0
```

Also print before heavy init. The universal demo prints `[BOOT]` before `sic_begin()` so an early SIC/bus/driver problem is visible instead of looking like a dead monitor.

**Duplicate symbols** — add `-DSIC_DISABLE_DUMMY=1` to disable the dummy fallback drivers.

**Keyboard dead** — check wiring and pull-ups; adjust debounce and scan period in `board_*.c`.

**`d` shows `mic: no` or `amp: no`** — the board descriptor or driver registration is missing.
For Cardputer original/v1.1 this should be `mic_pdm` + `ns4168`; check
`-DSIC_DRV_MIC_PDM=1`, `-DSIC_DRV_AMP_I2S=1`, and the `SIC_F_MIC` / `SIC_F_AMP` entries in
`board_cardputer.c`. For ES8311 boards, check `-DSIC_DRV_CODEC_ES8311=1` and entries pointing
to `"codec_es8311"`.

**`mic: start failed`** — I2S open or codec init failed. Verify:
- Cardputer PDM mic pins are DAT=G46 and CLK=G43.
- ES8311 targets: MCLK/BCLK/WS/DOUT/DIN pin numbers match hardware, `0x18` appears on I2C,
  `use_apll = false`, and I2S/MCLK starts before ES8311 I2C register writes.

**Mic silent / stuck at max value** — check `REG44` is `0x08` (not `0x58`). Value `0x58`
enables the DAC2ADC internal loopback, routing playback back into the ADC input.

**Amp produces no sound** — the DAC mute register is `REG31` (bits 6+5), not `REG37`.
Ensure `es_amp_enable` writes `0x00` to `REG31` to unmute, and that audio data is being
written to the I2S TX line via `sic_codec_write()` while the amp is enabled.

**Encoder not detected** — add `-DSIC_DRV_ENCODER_GPIO=1` and verify `sic_encoder_cfg_t`
pin numbers in `board_*.c`. GPIO pull-up is applied automatically by `encoder_gpio.c`.
