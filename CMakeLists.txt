# Tab5-Launchpad — ESP-IDF project root (M5Stack Tab5 / ESP32-P4).
#
# A dedicated USB-MIDI touchscreen grid controller firmware. Forked from
# Tab5-Orion (a multi-app launcher/retro-console for the same board): its
# reusable HAL (lib/SIC), graphics core (lib/SGFX) and console library
# (lib/konsole) are vendored verbatim; everything Orion-specific (the
# launcher, Doom/Quake/Wolf3D, the video player, dynamic ELF apps, USB
# host mode, WiFi/BT) is not.
#
# Plain, stock ESP-IDF project layout: this file plus a single `main/`
# component. Built with `idf.py build` against ESP-IDF 5.5.4, or through
# `tools/build.sh` using the same IDF CMake toolchain (see tools/idf-env.sh).

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tab5-launchpad)
