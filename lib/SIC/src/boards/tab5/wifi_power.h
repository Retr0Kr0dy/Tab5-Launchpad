/*
 * wifi_power.h — Tab5-private power sequencing for the ESP32-C6 Wi-Fi/BT
 * co-processor.
 *
 * Board-private plumbing, not public SIC API: lives under src/boards/tab5/
 * and is reached via a local include path ("boards/tab5/wifi_power.h") from
 * other files under src/, never via the public sic/ include namespace — the
 * same convention ioexpander.h and panel_detect.h use.
 *
 * Deliberately no SIC function ID and no registry driver: SIC owns only
 * getting the co-processor powered and out of reset. Everything above that
 * (the SDIO transport, esp-hosted, the Wi-Fi/BT protocol stacks) is
 * consumed directly by app code, so there is no app-facing capability here
 * for the registry to expose.
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Powers up and releases reset on the ESP32-C6 Wi-Fi/BT co-processor:
 * asserts its reset GPIO, enables WLAN_PWR_EN + antenna-select via the Tab5
 * GPIO expanders, waits, then releases reset. Does NOT touch the SDIO data
 * pins (CLK/CMD/D0-D3) or bring up any Wi-Fi protocol stack -- the Orion
 * application layer does that lazily through esp-hosted.
 * Requires tab5_ioexp_init() to have already run (uses tab5_ioexp_set()).
 */
void tab5_wifi_power_init(void);

/* Selects external (true) vs internal (false) antenna. Safe to call any
 * time after tab5_wifi_power_init(). */
void tab5_wifi_set_ext_antenna(bool ext);

#ifdef __cplusplus
}
#endif
