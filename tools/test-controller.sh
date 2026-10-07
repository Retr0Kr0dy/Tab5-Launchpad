#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I"$ROOT/main" -I"$ROOT/lib/SGFX/include" \
 "$ROOT/tools/test-controller.c" "$ROOT/main/launchpad_controller.c" \
 "$ROOT/main/launchpad_grid.c" "$ROOT/main/launchpad_modes.c" "$ROOT/main/studio_ui.c" \
 "$ROOT/lib/SGFX/src/core/gfx_core.c" "$ROOT/lib/SGFX/src/core/text/sgfx_font_builtin.c" \
 -lm -o "$OUT/controller-test"
"$OUT/controller-test"
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I"$ROOT/lib/SIC/include" -I"$ROOT/lib/SIC/src" \
 "$ROOT/tools/test-touch.c" -o "$OUT/touch-test"
"$OUT/touch-test"
