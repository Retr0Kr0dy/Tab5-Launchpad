# Tab5-Launchpad firmware as one ESP-IDF component.
#
# SIC / SGFX / konsole are *not* registered ESP-IDF components (no
# idf_component.yml of their own) -- portable libraries with plain
# src/+include/ trees, vendored from Tab5-Orion verbatim (see CMakeLists.txt
# at the project root). Their sources are globbed and compiled straight
# into this "main" component, same as Orion did.

set(ORION_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(SIC_DIR    ${ORION_ROOT}/lib/SIC)
set(SGFX_DIR   ${ORION_ROOT}/lib/SGFX)
set(KON_DIR    ${ORION_ROOT}/lib/konsole)

foreach(d ${SIC_DIR} ${SGFX_DIR} ${KON_DIR})
  if(NOT EXISTS ${d}/src)
    message(FATAL_ERROR "Missing ${d}/src -- the vendored library snapshot is incomplete.")
  endif()
endforeach()

# ---- Tab5-Launchpad app -----------------------------------------------------
set(ORION_SRCS
  ${CMAKE_CURRENT_LIST_DIR}/main.c
  ${CMAKE_CURRENT_LIST_DIR}/console.c
  ${CMAKE_CURRENT_LIST_DIR}/display.c
  ${CMAKE_CURRENT_LIST_DIR}/commands.c
  ${CMAKE_CURRENT_LIST_DIR}/shell.c
  ${CMAKE_CURRENT_LIST_DIR}/studio_ui.c
  ${CMAKE_CURRENT_LIST_DIR}/ui_theme.c
  ${CMAKE_CURRENT_LIST_DIR}/ui_components.c
  ${CMAKE_CURRENT_LIST_DIR}/settings_store.c
  ${CMAKE_CURRENT_LIST_DIR}/ymodem.c
  ${CMAKE_CURRENT_LIST_DIR}/usb_midi_device.c
  ${CMAKE_CURRENT_LIST_DIR}/launchpad_controller.c
  ${CMAKE_CURRENT_LIST_DIR}/launchpad_modes.c
  ${CMAKE_CURRENT_LIST_DIR}/launchpad_grid.c
  ${CMAKE_CURRENT_LIST_DIR}/launchpad_app.c
  ${CMAKE_CURRENT_LIST_DIR}/debug_overlay.c
  ${CMAKE_CURRENT_LIST_DIR}/settings_screen.c
  ${CMAKE_CURRENT_LIST_DIR}/diagnostics_screen.c
)

# ---- konsole ---------------------------------------------------------------
set(KON_SRCS
  ${KON_DIR}/src/konsole.c
  ${KON_DIR}/src/line.c
  ${KON_DIR}/src/cmd.c
  ${KON_DIR}/src/npf.c
)

# ---- SIC -------------------------------------------------------------------
file(GLOB_RECURSE SIC_SRCS ${SIC_DIR}/src/*.c)
list(FILTER SIC_SRCS EXCLUDE REGEX "_arduino\\.c$")
list(FILTER SIC_SRCS EXCLUDE REGEX "/backends/arduino/")
# Two dummy registration translation units exist in SIC (src/bus/dummy.c and
# src/drivers/dummy.c), both defining sic_register_driver_dummy() -- keep
# only the drivers/ copy (the superset), same exclusion Orion's own
# CMakeLists makes.
list(FILTER SIC_SRCS EXCLUDE REGEX "/src/bus/dummy\\.c$")

# ---- SGFX ------------------------------------------------------------------
file(GLOB_RECURSE SGFX_SRCS ${SGFX_DIR}/src/*.c)
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/arduino/")
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/stm32/")
# Tab5's only display transport is MIPI-DSI; drop the unreachable SPI/I2C
# HAL backends (same reasoning/exclusion as Orion's own CMakeLists: the
# legacy driver/i2c.h API espidf_i2c.c uses can't coexist at runtime with
# the new i2c_master driver everything else here uses).
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/espidf/espidf_spi\\.c$")
list(FILTER SGFX_SRCS EXCLUDE REGEX "/hal/espidf/espidf_i2c\\.c$")
list(FILTER SGFX_SRCS EXCLUDE REGEX "/sgfx_factory\\.c$")

idf_component_register(
  SRCS
    ${ORION_SRCS}
    ${KON_SRCS}
    ${SIC_SRCS}
    ${SGFX_SRCS}
  INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${SIC_DIR}/include
    ${SIC_DIR}/src            # board-private "boards/tab5/{ioexpander,panel_detect}.h"
    ${SGFX_DIR}/include
    ${KON_DIR}/include
  REQUIRES
    driver
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_i2s
    esp_driver_ledc
    esp_driver_sdmmc
    sdmmc
    esp_lcd
    esp_mm
    esp_psram
    esp_timer
    esp_hw_support
    spi_flash    # esp_flash.h   — SIC hal/hal_core_espidf.c
    fatfs        # esp_vfs_fat.h — SIC drivers/storage/sd_sdmmc_espidf.c
    espressif__esp_video         # pulled in only because SIC's autoreg couples the Tab5
                                 # camera driver to SIC_TARGET_TAB5+SIC_BACKEND_ESPIDF —
                                 # see sdkconfig.defaults' own comment on this
    espressif__esp_tinyusb       # usb_midi_device.c — USB-MIDI device mode
  PRIV_REQUIRES
    vfs
)

# ---- Build-time configuration ----------------------------------------------
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  ORION_VERSION="0.2.0"
  KONSOLE_FW_NAME="Tab5-Launchpad"
  KONSOLE_FW_VERSION="0.2.0"
  KONSOLE_ENABLE_ANSI=1

  # ---- SIC: ESP-IDF backend, Tab5 board profile ----
  SIC_BACKEND_ESPIDF=1
  SIC_TARGET_TAB5=1

  # sic_battery_read() is a single global symbol; Tab5's implementation is
  # the INA226 at 0x41. Without this the link fails on an undefined
  # sic_battery_read.
  SIC_BATTERY_INA226=1

  # ---- SGFX: MIPI-DSI, runtime-selected panel ----
  SGFX_HAL_ESPIDF=1
  SGFX_DRV_ILI9881C=1
  SGFX_DRV_ST7121=1
  SGFX_DRV_ST7123=1
)

# Vendored third-party sources: keep their warnings out of the way of ours.
set_source_files_properties(${SIC_SRCS} ${SGFX_SRCS} ${KON_SRCS}
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-function")
