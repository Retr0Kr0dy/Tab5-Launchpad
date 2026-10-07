# Cardputer pins

SIC supports two Cardputer keyboard backends behind the same logical keymap:

- **Cardputer original / v1.1**: 74HC138 + GPIO matrix.
- **Cardputer-ADV**: TCA8418 I2C keyboard controller.

The public application layer should not care which one is present. Use
`SIC_BOARD_CARDPUTER` or `SIC_BOARD_CARDPUTER_ADV` and consume `sic_key_poll()`.

## Cardputer original / v1.1

Keyboard matrix flags:

```ini
-DSIC_TARGET_CARDPUTER=1
-DKBD_SEL0=8
-DKBD_SEL1=9
-DKBD_SEL2=11
-DKBD_IN0=13
-DKBD_IN1=15
-DKBD_IN2=3
-DKBD_IN3=4
-DKBD_IN4=5
-DKBD_IN5=6
-DKBD_IN6=7
-DKBD_ACTIVE_LOW=1
-DKBD_PULL=UP
```

Legacy aliases are accepted for older projects:

```ini
-DKBD_A0=8 -DKBD_A1=9 -DKBD_A2=11 -DKBD_EN=<optional enable pin>
```

Other defaults commonly used by the demo:

```ini
-DI2C_SDA_PIN=2
-DI2C_SCL_PIN=1
-DSIC_MIC_DATA=46
-DSIC_MIC_CLK=43
-DSIC_SPK_BCLK=41
-DSIC_SPK_DOUT=42
-DSIC_SPK_WS=43
-DIR_TX_PIN=44
-DSIC_SD_CS=12
-DSIC_SD_MOSI=14
-DSIC_SD_SCK=40
-DSIC_SD_MISO=39
-DBAT_ADC_PIN=10
-DBAT_DIV_K=2.0f
```

## Cardputer-ADV

```ini
-DSIC_TARGET_CARDPUTER_ADV=1
-DSIC_NO_DRV_KBD_74HC138=1
-DSIC_DRV_KBD_TCA8418=1
-DSIC_DRV_CODEC_ES8311=1
-DSIC_DRV_IR_GPIO=1
-DSIC_DRV_SD_SPI=1
-DI2C_SDA_PIN=8
-DI2C_SCL_PIN=9
-DTCA8418_INT_PIN=11
-DES8311_BCLK_PIN=41
-DES8311_WS_PIN=43
-DES8311_DOUT_PIN=42
-DES8311_DIN_PIN=46
-DIR_TX_PIN=44
-DSIC_SD_CS=12
-DSIC_SD_MOSI=14
-DSIC_SD_SCK=40
-DSIC_SD_MISO=39
-DBAT_ADC_PIN=10
-DBAT_DIV_K=2.0f
```

The ADV uses the same 56-key logical layout as Cardputer, but its physical
keyboard backend is TCA8418 on I2C SDA/SCL G8/G9 with INT on G11. SIC normalizes
TCA8418 scan codes through `scan_to_index`, so the Cardputer keymap and Fn layer
are shared by both boards.

ADV audio is ES8311, not the original Cardputer PDM+direct-I2S path. The official
pin map exposes BCLK/SCLK=G41, LRCK=G43, ASDOUT(codec->ESP)=G46 and
DSDIN(ESP->codec)=G42. SIC therefore registers ADV mic and amp through
`codec_es8311`, plus IR and microSD through the same typed capabilities as the
original Cardputer.


Official M5Stack pin map used by SIC:

| Function | IC / signal | ESP32-S3 GPIO |
|---|---|---|
| PDM mic | SPM1423 DAT / CLK | G46 / G43 |
| Speaker | NS4168 BCLK / SDATA / LRCLK | G41 / G42 / G43 |
| IR TX | IR TX | G44 |
| microSD | CS / MOSI / CLK / MISO | G12 / G14 / G40 / G39 |
| Grove I2C | SDA / SCL | G2 / G1 |

`SIC_CAP_MIC`, `SIC_CAP_AUDIO_AMP`, `SIC_CAP_IR_TX`, and `SIC_CAP_SD` are
backed by typed SIC drivers on Cardputer. `SIC_CAP_SD` means the board has an
SD driver; `sic_sd_present()`/`sd_t::present()` still reports whether a card is
actually inserted and mountable.

## Logical key behavior

- `Shift` applies the normal US shifted layer.
- `Ctrl`, `Alt`, `Opt`, `Fn`, and `Shift` are exposed as `sic_key_event_t` flags.
- `Fn + \`` emits `SIC_KEY_ESC`.
- `Fn + Backspace` emits `SIC_KEY_DEL`.
- `Fn + ; , . /` emit Up/Left/Down/Right.
- `Fn + 1..0` emit F1..F10.
