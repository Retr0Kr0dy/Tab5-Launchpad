# konsole

Canonical repository: https://github.com/st4lk3r-unit/konsole

**konsole** is a lightweight, embeddable, cross-MCU command console in pure C.
It’s designed to drop into projects on **ESP32, STM32, nRF5, Arduino**, or any platform that offers basic I/O hooks.

The goals:

* **No external dependencies** - bare C, works in any firmware.
* **Configurable** - ANSI editing, history, clearing, prompt, banner.
* **Compact** - minimal RAM/flash footprint, tunable features.
* **Portable** - adapters for Arduino `Serial`, STM32 HAL UART, nRF5 app\_uart.
* **Convenient by default** - ANSI mode enabled, history, prompt, clear, help.


## Features

* Line editing with cursor movement (ANSI) or minimal “compat mode”.
* Command table with name, description, and handler callback.
* Built-in helpers:

  * `help` - list commands with descriptions
  * `clear` - clear the screen (ANSI or compat fallback)
* Command history (up/down arrows in ANSI mode).
* Works with most monitors:
  `screen`, `minicom`, `idf-monitor`, `pio device monitor`, etc.
* Extensible: just add to the command table.

## Folder structure

```
konsole/
├── adapters/        # platform-specific I/O shims
│   ├── arduino_serial.cpp
│   ├── stm32_hal_uart.c
│   └── nrf5_app_uart.c
├── konsole.c        # core console logic
├── konsole.h        # public API
├── line.c           # line editing
├── version.h
└── third_party/npf.c # tiny printf
```

Drop the whole `konsole/` folder into your project (e.g. under `src/`).

Include from C or C++:

```c
#include "konsole/konsole.h"
```

## Example usage (Arduino / PlatformIO)

**`src/main.cpp`:**

```cpp
#include <Arduino.h>
extern "C" {
  #include "konsole/konsole.h"
}

// Example commands
static int cmd_echo(struct konsole* ks, int argc, char** argv) {
  for (int i = 1; i < argc; i++)
    kon_printf(ks, "%s%s", argv[i], (i+1 < argc) ? " " : "\r\n");
  return 0;
}
static int cmd_sys(struct konsole* ks, int, char**) {
  kon_printf(ks, "uptime: %lu ms\r\n", (unsigned long)millis());
  return 0;
}
static int cmd_help(struct konsole* ks, int, char**) { kon_print_help(ks); return 0; }
static int cmd_clear(struct konsole* ks, int, char**) { kon_clear_screen(ks); return 0; }

// Command table
static const kon_cmd g_cmds[] = {
  {"help",  "list commands",    cmd_help},
  {"echo",  "echo arguments",   cmd_echo},
  {"sys",   "system info",      cmd_sys},
  {"clear", "clear the screen", cmd_clear},
};

static konsole g_ks;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  konsole_io io = {
    [](void*) -> size_t { return Serial.available(); },
    [](void*, uint8_t* b, size_t n) -> size_t {
      size_t i=0; while (i<n && Serial.available()) b[i++] = (uint8_t)Serial.read();
      return i;
    },
    [](void*, const uint8_t* b, size_t n) -> size_t { return Serial.write(b, n); },
    [](void*) -> uint32_t { return (uint32_t)millis(); },
    nullptr
  };

  // Initialize with ANSI mode by default (full editing/features)
  konsole_init(&g_ks, &io, g_cmds, sizeof(g_cmds)/sizeof(g_cmds[0]),
               "> ", /*vt100*/ true);

  kon_banner(&g_ks, "konsole ready");
}

void loop() {
  konsole_poll(&g_ks);
}
```

**`platformio.ini`:**

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_raw = yes
```

## Commands in action

```
konsole ready
> help
help   - list commands
echo   - echo arguments
sys    - system info
clear  - clear the screen

> echo hello world
hello world

> sys
uptime: 5321 ms
```

## Configuration

Adjustable via `#define`s in `konsole.h`:

* `KONSOLE_MAX_LINE` - maximum line length (default 128).
* `KONSOLE_HISTORY` - number of saved commands (default 8).
* `KONSOLE_CLEAR_COLS` - fallback clear width in compat mode (default 120).
* `KON_MODE_COMPAT` vs `KON_MODE_ANSI` - runtime modes.

* `DKONSOLE_FW_NAME` - custom firmware name (`\"custom\"`) 
* `DKONSOLE_FW_VERSION` - custom version (`\"v0.1\"`)


At runtime:

* `konsole_set_mode(ks, KON_MODE_COMPAT);`
* `konsole_set_mode(ks, KON_MODE_ANSI);`

## Porting

To run on a new MCU:

1. Implement the `konsole_io` struct:

   * `read_avail`
   * `read`
   * `write`
   * `millis`
2. Initialize with `konsole_init`.
3. Call `konsole_poll` in your main loop.

Adapters are provided for Arduino, STM32 HAL, nRF5.

## Notes

* Default mode is **ANSI** for convenience (editing, arrows, clear).
* In raw/limited monitors, switch to compat mode at runtime with `mode compat`.
* If you see artifacts in compat mode, increase `KONSOLE_CLEAR_COLS`.