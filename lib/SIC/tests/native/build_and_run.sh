#!/usr/bin/env bash
# build_and_run.sh — Phase 6 native link-check for SIC_TARGET_TAB5.
#
# Builds the platform-agnostic core + every pure-C99 Tab5 driver + the board
# descriptor against tests/native/posix_stub_backend.c (a test-only stand-in
# for the not-yet-implemented SIC_BACKEND_POSIX), and runs the resulting
# binary. ESP-IDF-only files (SD/camera) are intentionally excluded — see
# test_tab5_link.c's file header.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root (SIC/)

SRCS=(
  src/core/registry.c
  src/core/sic_core.c
  src/core/context.c
  src/core/sic_func.c
  src/core/autoreg.c
  src/drivers/dummy.c
  src/drivers/input/touch_gt911.c
  src/drivers/input/touch_st712x.c
  src/drivers/power/rtc_rx8130.c
  src/drivers/motion/imu_bmi270.c
  src/drivers/power/charger_tab5_ioexp.c
  src/power/battery_ina226.c
  src/drivers/audio/codec_es8388.c
  src/drivers/audio/codec_es7210.c
  src/boards/board_tab5.c
  src/boards/board_default.c
  src/boards/tab5/ioexpander.c
  src/boards/tab5/panel_detect.c
  src/boards/tab5/wifi_power.c
  src/audio/sic_audio.c
  src/audio/sic_audio_compat.c
  tests/native/posix_stub_backend.c
  tests/native/test_tab5_link.c
)

OUT=/tmp/sic_tab5_link_check
gcc -std=c99 -Wall -Wextra -Werror \
  -DSIC_TARGET_TAB5 -DSIC_BACKEND_POSIX -DSIC_BATTERY_INA226=1 \
  -DSIC_NO_DRV_KBD_74HC138 -DSIC_NO_DRV_TP4057 \
  -Iinclude -Isrc \
  "${SRCS[@]}" -o "$OUT"

echo "[build] OK -> $OUT"
"$OUT"
