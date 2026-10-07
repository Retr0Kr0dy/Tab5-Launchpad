#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-"$ROOT/launchpad-preview"}
mkdir -p "$OUT"
cc -std=gnu11 -O2 -Wall -Wextra -Werror \
  -I"$ROOT/main" -I"$ROOT/lib/SGFX/include" \
  "$ROOT/tools/launchpad_preview.c" \
  "$ROOT/main/studio_ui.c" "$ROOT/main/launchpad_grid.c" "$ROOT/main/launchpad_modes.c" \
  "$ROOT/lib/SGFX/src/core/gfx_core.c" \
  "$ROOT/lib/SGFX/src/core/text/sgfx_font_builtin.c" \
  -lm \
  -o "$OUT/orion-launchpad-preview"
"$OUT/orion-launchpad-preview" "$OUT"
printf 'Preview frames written to %s\n' "$OUT"
