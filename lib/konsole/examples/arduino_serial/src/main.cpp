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