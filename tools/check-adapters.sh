#!/usr/bin/env bash
# Syntax only, using ESP type declarations. This is NOT an ESP-IDF build.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fsyntax-only \
 -I"$ROOT/tools/host-stubs" -I"$ROOT/main" -I"$ROOT/lib/SGFX/include" \
 -I"$ROOT/lib/SIC/include" -I"$ROOT/lib/konsole/include" \
 "$ROOT/main/launchpad_app.c" "$ROOT/main/shell.c" \
 "$ROOT/main/settings_screen.c" "$ROOT/main/diagnostics_screen.c" \
 "$ROOT/main/debug_overlay.c"
echo 'Platform adapters: native syntax checks passed (ESP declarations stubbed).'
