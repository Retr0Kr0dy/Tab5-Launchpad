# SGFX Universal Feature Demo

A single demo that stress-tests SGFX features on both **mono (SSD1306)** and **color (ST77xx)** panels.  
It showcases: **text (5×7 & 8×8) with scaling + AA overlay, clipping, rotation sweep (with restore), color/mono-friendly fills & gradients, resilient BLIT (tries MONO1 then RGB565), and a simple perf HUD (FPS).**

## 1) What the demo does (scenes)

1. **Intro** — Title + resolution/rotation, corner marks.  
2. **Addressing** — Borders + midlines to verify coordinate origin and addressing.  
3. **Clipping** — Sets a clip window, draws checker & labels crossing the edges.  
4. **Rotation sweep** — Renders at rotations 0..3, then **restores** the boot rotation.  
5. **Text scaling** — 5×7 and 8×8 fonts at multiple scales.  
6. **BLIT demo** — “Resilient” blit tries **MONO1** first (SSD1306-friendly), then **RGB565** fallback (ST77xx).  
7. **Fills / gradient** — Horizontal color gradient + vertical bars; looks good on color, reasonable on mono.  
8. **Perf fills** — Checkerboard fills, prints average draw time.  
9. **Overlap merge** — Thin lines to expose page/byte merges on mono OLEDs.  
10. **Final** — “DEMO COMPLETE / PRESS RESET”.

While running, the **main loop** also animates five moving rectangles using a normalized framebuffer composition path and overlays a **FPS** HUD (antialiased A8 overlay blended into the RGBA framebuffer).

## 2) Requirements

- **Toolchain:** C++11 or later (Arduino uses C++ by default).  
- **Targets:** Arduino frameworks for ESP32/STM32/RP2040 are known-good.  
- **Displays:**  
  - **SSD1306** (128×64 typical, I2C or SPI)  
  - **ST7735** (160×128), **ST7789** (240×320), **ST7796** (320×480)

## 3) Build flags (must set)

Pick a **bus**, a **driver**, **panel size**, and **pins/speeds**:

| What                    | Define(s)                                             | Notes |
|------------------------:|--------------------------------------------------------|------|
| Bus                     | `SGFX_BUS_SPI` **or** `SGFX_BUS_I2C`                 | pick one |
| Driver                  | `SGFX_DRV_SSD1306` / `SGFX_DRV_ST7735` / `SGFX_DRV_ST7789` / `SGFX_DRV_ST7796` | |
| Panel size              | `SGFX_W`, `SGFX_H`                                    | logical drawing size |
| SPI pins                | `SGFX_PIN_SCK`, `SGFX_PIN_MOSI`, `SGFX_PIN_CS`, `SGFX_PIN_DC`, `SGFX_PIN_RST` | required for SPI |
| I2C pins & addr         | `SGFX_PIN_SCL`, `SGFX_PIN_SDA`, `SGFX_I2C_ADDR`       | required for I2C (0x3C/0x3D are common) |
| Bus speed               | `SGFX_SPI_HZ` or `SGFX_I2C_HZ`                        | e.g. SPI 24–40 MHz; I2C 100–400 kHz |
| Rotation at boot        | `SGFX_ROT`                                            | 0..3 |
| Color order (ST77xx)    | `SGFX_DEFAULT_BGR_ORDER`                                    | 0=RGB, **1=BGR** (often required) |
| ST77xx offsets          | `SGFX_COLSTART`, `SGFX_ROWSTART`                       | module dependent |

**Bus defaults (quick ref)** — SPI **MODE0**, MSB-first. I2C 100–400 kHz.

### Optional toggles
- `SGFX_SCRATCH_BYTES` — size of the static scratch buffer (default **4096**).  
  - For the built-in sprites: MONO1 needs `W*H/8` bytes (here 32×16 → 64 B), RGB565 needs `2*W*H` (32×16 → 1024 B).  
- `SGFX_DEMO_FORCE_MONO1` or `SGFX_DEMO_FORCE_RGB565` — force a particular blit path.  
- `SGFX_LOG_DEBUG=1` — enable verbose logs in SGFX.  
- Tile/presenter tuning (if compiled in): `SGFX_MAX_LINE_PX`, `SGFX_TILE_W`, `SGFX_TILE_H`.

### ESP32-S3 + ST7789 240×320 (SPI)
```ini
[env:esp32s3-st7789]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
build_src_filter = +<src/main.cpp>
build_flags =
  -I ../../include       ; or: -I include  (point to SGFX headers)
  -D SGFX_BUS_SPI
  -D SGFX_DRV_ST7789
  -D SGFX_W=240 -D SGFX_H=320
  -D SGFX_PIN_SCK=36 -D SGFX_PIN_MOSI=35 -D SGFX_PIN_CS=34 -D SGFX_PIN_DC=33 -D SGFX_PIN_RST=37
  -D SGFX_SPI_HZ=40000000
  -D SGFX_DEFAULT_BGR_ORDER=1
  -D SGFX_ROT=0
monitor_speed = 115200
````

### RP2040 + SSD1306 128×64 (I2C @ 0x3C)

```ini
[env:pico-ssd1306]
platform = raspberrypi
board = pico
framework = arduino
build_src_filter = +<src/main.cpp>
build_flags =
  -I ../../include       ; or: -I include
  -D SGFX_BUS_I2C
  -D SGFX_DRV_SSD1306
  -D SGFX_W=128 -D SGFX_H=64
  -D SGFX_PIN_SCL=5 -D SGFX_PIN_SDA=4
  -D SGFX_I2C_ADDR=0x3C
  -D SGFX_I2C_HZ=400000
  -D SGFX_ROT=0
monitor_speed = 115200
```

### STM32F103 + ST7735 160×128 (SPI)

```ini
[env:bluepill-st7735]
platform = ststm32
board = genericSTM32F103C8
framework = arduino
build_src_filter = +<src/main.cpp>
build_flags =
  -I ../../include       ; or: -I include
  -D SGFX_BUS_SPI
  -D SGFX_DRV_ST7735
  -D SGFX_W=160 -D SGFX_H=128
  -D SGFX_PIN_SCK=13 -D SGFX_PIN_MOSI=15 -D SGFX_PIN_CS=12 -D SGFX_PIN_DC=11 -D SGFX_PIN_RST=10
  -D SGFX_SPI_HZ=24000000
  -D SGFX_DEFAULT_ROTATION=1
  -D SGFX_DEFAULT_BGR_ORDER=0
  -D SGFX_COLSTART=0 -D SGFX_ROWSTART=0
  -D SGFX_ROT=1
monitor_speed = 115200
```

## 5) Code structure notes (what to look for)

* **Arduino vs POSIX**:
  Under Arduino, the code uses `delay()`/`millis()`. On POSIX it provides `SGFX_DELAY()` and `SGFX_MILLIS()` shims via `nanosleep/clock_gettime` so you can run logic headless for timing (no framebuffer dump unless you provide a POSIX HAL).

* **Autoinit**:
  `sgfx_autoinit(&dev, scratch, sizeof scratch)` picks up your **build flags** (bus/driver/pins/speeds) and initializes the device.
  If it fails at boot, `setup()` sits in a safe loop; the `loop()` also tries one **lazy re-init** pass.

* **FB vs device paths**:
  Many scenes attempt an **RGBA framebuffer path** (`sgfx_fb_create`, then `sgfx_present_frame`). If FB creation or driver “present” path isn’t available, scenes gracefully **fallback** to device drawing.

* **AA overlay**:
  The FPS HUD uses an 8-bit alpha overlay (`a8_fb_t`) blended into the RGBA FB (non-premultiplied alpha). Only changed regions are marked dirty: `sgfx_fb_mark_dirty_px(...)`.

* **Resilient BLIT**:
  `try_blit_any*()` builds a small sprite, then attempts **MONO1** first (ideal for SSD1306), falling back to **RGB565** if not supported or if mono resources are constrained.

## 6) Troubleshooting

* **Blank/white screen** → verify CS/DC/RST wiring; for ST77xx try `-DSGFX_DEFAULT_BGR_ORDER=1`.
* **Shifted image** → set `-DSGFX_COLSTART` / `-DSGFX_ROWSTART`.
* **Flicker/tearing (SPI)** → lower `SGFX_SPI_HZ` or increase `SGFX_MAX_LINE_PX`.
* **I2C lockups** → confirm address (0x3C/0x3D) and stay at 100–400 kHz first.
* **Wrong orientation** → check `SGFX_ROT`, `SGFX_DEFAULT_ROTATION`, and `SGFX_W/H`.
* **FB path fails** → device/driver might not support present-by-window; the demo will fallback automatically.

## 7) Notes for power users

* **Memory use**: FB = `4 * W * H` bytes; line buffer ≈ `2 * max_line_px`; scratch as defined by `SGFX_SCRATCH_BYTES`.
* **DMA/alignment**: Some MCUs prefer 4-byte aligned buffers for SPI DMA.
* **Multiple displays**: The demo uses a single global `sgfx_device_t dev`. Multi-display needs separate device instances and careful bus sharing.
