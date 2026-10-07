# SGFX — Portable Graphics Library

Canonical repository: https://github.com/st4lk3r-unit/SGFX

A tiny, **hardware-agnostic** C99 2D graphics library for microcontrollers and small displays. SGFX keeps a simple frame buffer in RAM (RGB565 or RGBA8888) and streams it efficiently to a panel driver via thin HAL shims (SPI/I²C). It aims to be **portable, predictable, and hackable**.

## Highlights

- Single public headers: `include/sgfx.h`, `include/sgfx_fb.h`, `include/sgfx_text.h`
- Color formats: **RGB565** (default) or **RGBA8888**
- Drivers: **SSD1306** (I²C) and **ST77xx** family (SPI, e.g. ST7789)
- HALs: Arduino-style, ESP-IDF, STM32, RP2040 (via thin bus wrappers)
- New text engine (`sgfx_text.h`): SDF/bitmap fonts, styles (outline, shadow, bold), top/bottom anchors, legacy 5×7 compatibility wrapper
- Presenter: tiled/line-based `sgfx_present_*` to stream FB to panels with limited RAM (set the line budget via function arguments)
- Clear config via compile-time flags (`sgfx_port.h`, `sgfx_config.h`)

## Install

You can drop the `include/` and `src/` folders into your project, or add SGFX as a library in PlatformIO.

**PlatformIO example**
```ini
[env:esp32s3-st7789]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
build_flags =
  -I include            ; ensure SGFX headers are visible
  -DSGFX_COLOR_RGB565=1 ; or -DSGFX_COLOR_RGBA8888=1
  -DSGFX_DEFAULT_ROTATION=0
  -DSGFX_COLSTART=0 -DSGFX_ROWSTART=0
  -DSGFX_DEFAULT_BGR_ORDER=1     ; many ST77xx boards are BGR
lib_deps =
  ; (add any bus/board deps you need)
```

## Quick Start

```c
#include "sgfx.h"
#include "sgfx_fb.h"
#include "sgfx_text.h"

sgfx_device_t dev = 0;
sgfx_fb_t fb = 0;
sgfx_present_t pr = 0;

/* 1) Open the device (choose SPI or I2C and your driver cfg) */
sgfx_hal_cfg_spi_t bus = { /* pins, freq, etc. */ };
extern const sgfx_driver_ops_t SGFX_DRV_ST7789;
sgfx_drv_cfg_st77xx_t pane = {
  .w = 240, .h = 320,
  .colstart = SGFX_COLSTART, .rowstart = SGFX_ROWSTART,
  .bgr = SGFX_DEFAULT_BGR_ORDER, .invert = SGFX_DEFAULT_INVERT,
  .rotation = SGFX_DEFAULT_ROTATION
};
SGFX_OK == sgfx_open_spi(&dev, &bus, &SGFX_DRV_ST7789, &pane);

/* 2) Allocate a framebuffer (RGB565 by default) */
SGFX_OK == sgfx_fb_create(&fb, 240, 320, /*line_budget_px*/ 240, /*tile_h*/ 16);

/* 3) Draw */
fb_full_clear(&fb, BLACK());
fb_draw_rect(&fb, 0,0, fb.w, fb.h, WHITE());

sgfx_font_t* F = sgfx_font_open_builtin();
sgfx_text_style_t st = sgfx_text_style_default(WHITE(), 18.f);
sgfx_text_draw_line(&fb, 8, 24, "Hello SGFX!", F, &st);

/* 4) Present (stream to the panel) */
sgfx_present_init(&pr, 240);  /* max_line_px */
sgfx_present_frame(&pr, &dev, &fb);
sgfx_present_deinit(&pr);

/* 5) Cleanup */
sgfx_fb_destroy(&fb);
```

## API Surface (as of `include/`)

### Core Types (`sgfx.h`)
- `sgfx_device_t` — logical device combining **HAL bus** and **driver ops**
- `sgfx_driver_ops_t` — driver vtable (init, window, write-lines…)
- `sgfx_hal_*` — bus abstractions for SPI/I²C + GPIO
- Device helpers:
  - `int sgfx_open_spi(sgfx_device_t*, const sgfx_hal_cfg_spi_t*, const sgfx_driver_ops_t*, const void* drv_cfg);`
  - `int sgfx_open_i2c(sgfx_device_t*, const sgfx_hal_cfg_i2c_t*, const sgfx_driver_ops_t*, const void* drv_cfg);`

### Framebuffer & Presenter (`sgfx_fb.h`)
- Color selection (compile-time):
  - `SGFX_COLOR_RGB565` (default) or `SGFX_COLOR_RGBA8888`
- FB:
  - `int sgfx_fb_create(sgfx_fb_t* fb, int w, int h, int max_line_px, int tile_h);`
  - `void sgfx_fb_destroy(sgfx_fb_t*);`
  - Drawing: `fb_full_clear`, `fb_draw_fast_hline/vline`, `fb_draw_rect`, `fb_fill_rect`, `fb_blit_rgb`, …
- Present:
  - `int sgfx_present_init(sgfx_present_t*, int max_line_px);`  ← set your DMA/line budget here
  - `int sgfx_present_frame(sgfx_present_t*, sgfx_device_t*, sgfx_fb_t*);`
  - `void sgfx_present_deinit(sgfx_present_t*);`
- Utilities:
  - `void sgfx_fb_blit_a8(...)` — blend an Alpha8 sprite into RGB565/RGBA8888

### Text (`sgfx_text.h`)
- Font kinds: `SGFX_FONT_BITMAP_A8`, `SGFX_FONT_SDF_A8`
- Open/close:
  - `sgfx_font_t* sgfx_font_open_builtin(void);` (5×7 ASCII)
  - (SDF loader hook: if present in your build)
- Draw:
  - `sgfx_text_style_t sgfx_text_style_default(sgfx_rgba8_t color, float px);`
  - `int sgfx_text_draw_line(sgfx_fb_t* fb, int x, int y, const char* utf8, sgfx_font_t* F, const sgfx_text_style_t* st);`
- Legacy:
  - `#include "sgfx_text_legacy_compat.h"`
  - `fb_draw_5x7_compat(fb, x, y, "Hi", WHITE());`

## Configuration (`sgfx_port.h` / `sgfx_config.h`)

Set via your build system (e.g., `-D` flags) or edit the headers.

- **Orientation & Offsets**
  - `SGFX_DEFAULT_ROTATION` (0..3)
  - `SGFX_COLSTART`, `SGFX_ROWSTART` (ST77xx panels)
  - `SGFX_DEFAULT_BGR_ORDER` (0/1) — many ST77xx are BGR
  - `SGFX_DEFAULT_INVERT` (0/1)

- **Color format**
  - `SGFX_COLOR_RGB565=1` (default) or `SGFX_COLOR_RGBA8888=1`

- **Presenter & Memory**
  - Line budget is chosen at runtime via `sgfx_fb_create(..., max_line_px, ...)` and `sgfx_present_init(..., max_line_px)`
  - FB RAM ≈ `W * H * BYTESPP`

**Rule of thumb**
```
SSD1306 128×64:   FB ~  8 KB (monochrome via driver path)
ST7789  240×320:  FB ~ 150 KB (RGB565)
```

## Boards / Drivers

- **SSD1306 (I²C)** — monochrome OLED, auto-converts RGB565 text/graphics
- **ST77xx (SPI)** — tested with ST7789 240×320; set BGR/offsets/rotation
- Extend by implementing a `sgfx_driver_ops_t` (init, set_window, write_lines)

## Example (from `examples/example_wrapup/`)

The demo showcases:
- text sizing (SM/MD/LG), SDF fallback to 5×7
- top/bottom anchored helpers to avoid border clipping
- single `SCENE_PAUSE_MS` to tune pacing
- optional double-FB via `SGFX_DEMO_DOUBLE_FB`

Build it by copying `src/main.cpp` into your project and adapting the pins in your HAL bus config.

# SGFX Support Matrix

**Scope:** auto-generated from this repo’s `src/drivers` and `include/` headers.

Use these flags in `platformio.ini` (or your build) to select bus/driver and configure the panel.


## Drivers & Panels

| Panel | Bus | Color | Required flags | Common options | Notes |
|------|-----|-------|----------------|----------------|-------|
| **ST7735** | SPI | Color (RGB565) | `-DSGFX_BUS_SPI=1`, `-DSGFX_DRV_ST7735=1`, `-DSGFX_W=<px>`, `-DSGFX_H=<px>` | `-DSGFX_SPI_HZ=<32000000>`, `-DSGFX_DEFAULT_BGR_ORDER=<0\|1>` , `-DSGFX_COLSTART=<n>`, `-DSGFX_ROWSTART=<n>`, `-DSGFX_DEFAULT_INVERT=<0\|1>` | Many ST77xx boards need `BGR_ORDER=1`; set offsets if image is shifted. |
| **ST7789** | SPI | Color (RGB565) | `-DSGFX_BUS_SPI=1`, `-DSGFX_DRV_ST7789=1`, `-DSGFX_W=<px>`, `-DSGFX_H=<px>` | `-DSGFX_SPI_HZ=<32000000>`, `-DSGFX_DEFAULT_BGR_ORDER=<0\|1>`, `-DSGFX_COLSTART=<n>`, `-DSGFX_ROWSTART=<n>`, `-DSGFX_DEFAULT_INVERT=<0\|1>` | Many ST77xx boards need `BGR_ORDER=1`; set offsets if image is shifted. |
| **ST7796** | SPI | Color (RGB565) | `-DSGFX_BUS_SPI=1`, `-DSGFX_DRV_ST7796=1`, `-DSGFX_W=<px>`, `-DSGFX_H=<px>` | `-DSGFX_SPI_HZ=<32000000>`, `-DSGFX_DEFAULT_BGR_ORDER=<0\|1>`, `-DSGFX_COLSTART=<n>`, `-DSGFX_ROWSTART=<n>`, `-DSGFX_DEFAULT_INVERT=<0\|1>` | Many ST77xx boards need `BGR_ORDER=1`; set offsets if image is shifted. |
| **SSD1306** | I2C | Monochrome (1bpp path via driver) | `-DSGFX_BUS_I2C=1`, `-DSGFX_DRV_SSD1306=1`, `-DSGFX_W=<px>`, `-DSGFX_H=<px>` | `-DSGFX_I2C_ADDR=<0x3C\|0x3D>`, `-DSGFX_I2C_HZ=<400000>` | Auto-converts to mono; choose correct I2C addr; small RAM footprint. |

## HAL/Buses & Pins (from `sgfx_port.h`)

- Define **display geometry**: `-DSGFX_W`, `-DSGFX_H`, optional `-DSGFX_ROT` (0..3).

- **Bus select:** `-DSGFX_BUS_SPI=1` or `-DSGFX_BUS_I2C=1`.

- **Driver select:** one of `-DSGFX_DRV_SSD1306`, `-DSGFX_DRV_ST7735`, `-DSGFX_DRV_ST7789`, `-DSGFX_DRV_ST7796`.

- **SPI pins:** `SGFX_PIN_SCK`, `SGFX_PIN_MOSI`, `SGFX_PIN_MISO`, `SGFX_PIN_CS`, `SGFX_PIN_DC`, `SGFX_PIN_RST`, `SGFX_PIN_BL`.

- **I2C params:** `SGFX_I2C_ADDR`, `SGFX_I2C_HZ`.

- **Speeds:** `SGFX_SPI_HZ` for SPI, `SGFX_I2C_HZ` for I2C.


## Color & Panel Defaults

- **Color format:** `SGFX_COLOR_RGB565` (default) or `SGFX_COLOR_RGBA8888`.

- **Panel defaults:** `SGFX_DEFAULT_ROTATION`, `SGFX_DEFAULT_BGR_ORDER`, `SGFX_DEFAULT_INVERT`, `SGFX_COLSTART`, `SGFX_ROWSTART`.


> Tip: If colors look swapped on ST77xx, set `-DSGFX_DEFAULT_BGR_ORDER=1`.

# SGFX API Cheatsheet (annotated)

**Source of truth:** generated from `include/*.h` in this repo. Short, practical notes added to each call.

> Tip: examples sometimes define local helpers (e.g. `fb_draw_fast_hline` inside a demo). This sheet lists **public APIs** from headers.

## Core Types

- `sgfx_bus` — HAL bus handle (SPI/I²C pins + speed config).
- `sgfx_device` — Logical device: HAL bus + panel driver + clip/rotation state.
- `sgfx_font` — Opaque font handle (builtin 5×7 or SDF/bitmap loaded at runtime).
- `sgfx_hal_cfg_i2c` — I²C bus configuration (pins, address, frequency).
- `sgfx_hal_cfg_spi` — SPI bus configuration (pins, CS/DC/BL, frequency).

## Device/Core

- `sgfx_open_i2c(...)` — Open a device on an **I²C** bus: pass `sgfx_hal_cfg_i2c`, a driver ops table (e.g. `SGFX_DRV_SSD1306`), and a driver-specific panel cfg.
- `sgfx_open_spi(...)` — Open a device on an **SPI** bus: pass `sgfx_hal_cfg_spi`, driver ops (e.g. `SGFX_DRV_ST7789`), and panel cfg.
- `sgfx_init(...)` — Low-level initializer behind the helpers; use when doing custom bring-up (you supply bus + driver + caps + scratch).
- `sgfx_set_rotation(dev, rot)` — Set logical rotation **0..3** (combines with physical offsets/BGR in config).
- `sgfx_set_clip(dev, x,y,w,h)` — Limit subsequent draws to a rectangle (device-space clipping).
- `sgfx_reset_clip(dev)` — Restore full-surface clip.
- `sgfx_set_palette(dev, const sgfx_rgba8_t* p, int count)` — Optional indexed/mono → color mapping; safe to ignore for RGB565 paths.
- `sgfx_set_dither(dev, enable)` — Toggle ordered dithering on down-conversion (useful mono/OLED paths).
- `sgfx_draw_pixel(dev, x,y, color)` — Plot one pixel (clipped).
- `sgfx_draw_fast_hline(dev, x,y, w, color)` — Fast 1‑px **horizontal** line.
- `sgfx_draw_fast_vline(dev, x,y, h, color)` — Fast 1‑px **vertical** line.
- `sgfx_draw_rect(dev, x,y, w,h, color)` — Stroke rectangle (1‑px outline).
- `sgfx_fill_rect(dev, x,y, w,h, color)` — Fill rectangle (clipped).
- `sgfx_blit(dev, x,y, w,h, const void* pixels, int stride_bytes)` — Push a raw pixel block (format must match device color mode).
- `sgfx_clear(dev, color)` — Clear whole surface to a color.

**Return values:** Most calls return `SGFX_OK` (0) or a negative error (`SGFX_ERR_INVAL`, `SGFX_ERR_NOMEM`, `SGFX_ERR_NOSUP`, `SGFX_ERR_EIO`).

## Framebuffer & Presenter

- `sgfx_fb_create(fb, W,H, max_line_px, tile_h)` — Allocate a RAM framebuffer and internal tile metadata.
  - **`max_line_px`**: DMA/streaming line budget; choose ≤ your panel width and memory constraints.
  - **`tile_h`**: Tile height for dirty-rect tracking (e.g., 16).
- `sgfx_fb_destroy(fb)` — Free framebuffer + metadata.
- `sgfx_fb_fill_rect_px(fb, x,y, w,h, color)` — Fill **raw FB pixels** (no device clipping; then mark dirty).
- `sgfx_fb_mark_dirty_px(fb, x,y, w,h)` — Manually mark a region dirty (if you wrote pixels directly).
- `sgfx_fb_rehash_tiles(fb)` — Recompute tile hashes (useful after bulk pixel writes).
- `sgfx_fb_blit_a8(fb, x,y, a8, pitch, w,h, color)` — Blend an **alpha8** sprite into RGB565/RGBA8888 FB using a solid color.
- `sgfx_present_init(pr, max_line_px)` — Initialize the presenter with your line budget (same unit as FB create).
- `sgfx_present_frame(pr, dev, fb)` — Stream dirty lines/tiles from FB to panel (SPI/I²C) efficiently.
- `sgfx_present_deinit(pr)` — Release presenter resources (if any).
- `sgfx_present_stats_reset(pr)` — Zero presenter counters (if you read stats in your build).

## Text & Fonts

- `sgfx_font_open_builtin()` — Get the built‑in **5×7 ASCII** bitmap font (tiny; always available).
- `sgfx_font_load_from_memory(kind, data, bytes, ...)` — Load a **bitmap A8** or **SDF A8** font from memory.
- `sgfx_font_load_from_stream(kind, read_cb, user, ...)` — Stream‑loader variant for large fonts.
- `sgfx_font_close(F)` — Free a font you opened/loaded.
- `sgfx_text_style_default(color, px)` — Convenience: build a style with size, color, and sane defaults.
- `sgfx_text_draw_line(fb, x,y, "utf8", F, &style)` — Draw a single **UTF‑8** line into the FB.
- `sgfx_text_measure_line("utf8", F, &style, &out_w, &out_h)` — Measure rendered width/height (layout before drawing).

## Build-Time Macros

From **`sgfx_port.h`** (wiring/bring‑up):
- `SGFX_W`, `SGFX_H` — Logical width/height in pixels (required).
- `SGFX_ROT` — Boot rotation (0..3).
- `SGFX_STRICT_RGB565` — Force strict RGB565 paths (debug/consistency).
- `SGFX__DRV_CAPS`, `SGFX__DRV_OPS` — Advanced: inject driver caps/ops when doing custom init.

From **`sgfx_config.h`** (panel defaults):
- `SGFX_DEFAULT_ROTATION` — Default rotation at init (combines with `SGFX_ROT`).
- `SGFX_DEFAULT_BGR_ORDER` — 0=RGB, **1=BGR** (many ST77xx panels need 1).
- `SGFX_DEFAULT_INVERT` — Invert panel pixels if your module requires it.
- `SGFX_COLSTART`, `SGFX_ROWSTART` — Column/row offsets for ST77xx variants.

## Minimal Flow

```c
// 1) Build flags: bus/driver/W/H/pins/speeds + panel defaults.
// 2) Open the device:
sgfx_open_spi(&dev, &spi_cfg, &SGFX_DRV_ST7789, &pane_cfg);
// or:
sgfx_open_i2c(&dev, &i2c_cfg, &SGFX_DRV_SSD1306, &oled_cfg);

// 3) Create a framebuffer (choose a sensible line budget/tile height):
sgfx_fb_create(&fb, W, H, /*max_line_px*/ W, /*tile_h*/ 16);

// 4) Draw:
sgfx_clear(&dev, BLACK());
sgfx_fill_rect(&dev, 0,0, W,H, BLACK());

sgfx_font_t* F = sgfx_font_open_builtin();
sgfx_text_style_t st = sgfx_text_style_default(WHITE(), 14.f);
sgfx_text_draw_line(&fb, 4, 16, "Hello", F, &st);

// 5) Present:
sgfx_present_init(&pr, /*max_line_px*/ W);
sgfx_present_frame(&pr, &dev, &fb);
sgfx_present_deinit(&pr);

// 6) Cleanup:
sgfx_fb_destroy(&fb);
```
