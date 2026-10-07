# SIC — St4lk3r Integrated Chips

Canonical repository: https://github.com/st4lk3r-unit/SIC

SIC is a small hardware abstraction layer for broad-compatible embedded firmware.
Consume it with:

```cpp
#include <sic/sic.h>
```

The rule of the project is:

> Application code describes intent. PlatformIO build targets select hardware reality.

A normal app/example should not check whether it is running on Cardputer, Cardputer-ADV, T-Pager, etc. It should call SIC APIs and let the selected board backend provide keyboard, battery, audio, encoder, bus, and other drivers.

## 5-minute quickstart

Run the same universal example on different boards by changing only `-e`:

```bash
cd SIC/examples/Universal-Demo
pio run -e cardputer -t upload
pio device monitor -b 115200
```

```bash
cd SIC/examples/Universal-Demo
pio run -e cardputer-adv -t upload
pio device monitor -b 115200
```

```bash
cd SIC/examples/Universal-Demo
pio run -e tpager -t upload
pio device monitor -b 115200
```

The example source stays the same:

```cpp
const sic_board_t* board = sic_board_default();
sic_begin_opts_t opts = { 1, 0 };
sic_begin(board, &opts);

sic_key_event_t ev;
while (sic_key_poll(&ev) > 0) {
  // consume abstract key events
}
```

The selected PlatformIO environment sets flags such as `SIC_TARGET_CARDPUTER`, `SIC_TARGET_TPAGER`, bus pins, and driver enables. Those flags belong in `platformio.ini` and SIC board backends, not in app logic.

The universal demo commands are intentionally capability-driven: `m` records five seconds from `sic_mic(0)` and plays it through `sic_amp(0)`, `p` beeps for two seconds, `r` sends a NEC IR test frame, and `f` probes/mounts SD when an SD driver is present. The source does not branch on board names.

## Using SIC in your own PlatformIO project

```ini
[env:my-board]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
lib_deps =
  ../SIC
build_flags =
  ; ESP32-S3 native USB console. Required on Cardputer/StampS3-style boards
  ; when you expect Arduino `Serial` to appear in PlatformIO monitor.
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MSC_ON_BOOT=0
  -DARDUINO_USB_DFU_ON_BOOT=0
  -DSIC_TARGET_TPAGER=1
  -DI2C_SDA_PIN=3
  -DI2C_SCL_PIN=2
  -DSIC_DRV_KBD_TCA8418=1
```

```cpp
#include <Arduino.h>
#include <sic/sic.h>

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 3000) delay(10);
  Serial.println("[BOOT] app starting");

  sic_begin_opts_t opts;
  opts.init_buses = 1;
  opts.lazy_drivers = 0;

  int rc = sic_begin(sic_board_default(), &opts);
  Serial.printf("[SIC] rc=%d\n", rc);
}

void loop() {
  sic_key_event_t ev;
  while (sic_key_poll(&ev) > 0) {
    if (ev.pressed && ev.ascii) Serial.write(ev.ascii);
  }
}
```

## Repo layout

- `include/` — public headers (`#include <sic/sic.h>`)
- `src/` — boards, drivers, HAL, core registry
- `examples/Universal-Demo/` — one source tree for all supported board targets
- `docs/` — feature matrix, porting guide, pins, API, troubleshooting
- `templates/` — driver authoring template
- `tests/` — native sanity tests

## Tests

```bash
cd SIC/tests/native
make
./out/test_sic_native
```

## Design references

See `docs/DESIGN_INVARIANTS.md` for the abstraction rules and `docs/FEATURE_MATRIX.md` for current backend coverage.
