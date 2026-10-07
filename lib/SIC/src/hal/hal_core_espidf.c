/*
 * hal_core_espidf.c — ESP-IDF native backend: sic_sysinfo().
 *
 * Counterpart of src/hal/hal_core.cpp (Arduino). All platform-agnostic core
 * functions (sic_begin, sic_has, sic_ir_send_nec, sic_charger_state, …) stay
 * in src/core/sic_core.c; only sic_sysinfo() is platform-specific.
 *
 * Board-agnostic and radio-agnostic by construction:
 *   - every field is derived from a real chip query, nothing is hardcoded to
 *     a particular SKU (the Arduino backend hardcodes chip_model, flash size
 *     and psram size, which is wrong on anything but the board it was written
 *     for);
 *   - no <Arduino.h>, and crucially no <WiFi.h>. The Arduino backend reaches
 *     into the Wi-Fi driver just to fetch a MAC address; on targets without a
 *     Wi-Fi peripheral (ESP32-P4, whose radio is an external co-processor)
 *     that is both semantically wrong and a hard build failure.
 */
#if defined(SIC_BACKEND_ESPIDF)

#include <stdint.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_mac.h"

/* esp_clk_cpu_freq() lives under esp_private/, but it is the only stable,
 * target-independent "what is the CPU running at right now" query across the
 * whole ESP-IDF v5 line (the public esp_clk_tree_src_get_freq_hz() needs a
 * soc_module_clk_t and pulls in soc-specific clock-tree headers, and
 * rtc_clk_cpu_freq_get_config() is equally private but less portable). It is
 * what IDF's own components and the Arduino core use for this purpose. */
#include "esp_private/esp_clk.h"

#if defined(CONFIG_SPIRAM)
#include "esp_psram.h"
#endif

#include "sic/hal.h"
#include "sic/sic.h"

/*
 * Flash SPI clock is a build-time setting; the flash driver exposes no runtime
 * "what is the SPI bus clocked at" query, so it is derived from Kconfig here.
 * Some targets (ESP32-P4) publish a numeric CONFIG_ESPTOOLPY_FLASHFREQ_VAL in
 * MHz — prefer it when present; otherwise fall back to the per-option boolean
 * symbols, which every target defines.
 */
#if   defined(CONFIG_ESPTOOLPY_FLASHFREQ_VAL)
#  define SIC_FLASH_HZ ((uint32_t)CONFIG_ESPTOOLPY_FLASHFREQ_VAL * 1000000u)
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_120M)
#  define SIC_FLASH_HZ 120000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_80M)
#  define SIC_FLASH_HZ  80000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_64M)
#  define SIC_FLASH_HZ  64000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_60M)
#  define SIC_FLASH_HZ  60000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_48M)
#  define SIC_FLASH_HZ  48000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_40M)
#  define SIC_FLASH_HZ  40000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_32M)
#  define SIC_FLASH_HZ  32000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_30M)
#  define SIC_FLASH_HZ  30000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_26M)
#  define SIC_FLASH_HZ  26000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_24M)
#  define SIC_FLASH_HZ  24000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_20M)
#  define SIC_FLASH_HZ  20000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_16M)
#  define SIC_FLASH_HZ  16000000u
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_12M)
#  define SIC_FLASH_HZ  12000000u
#else
/* No matching Kconfig symbol: report 0 ("unknown") rather than inventing a
 * plausible-looking number a caller might trust. */
#  define SIC_FLASH_HZ 0u
#endif

static const char* sic_chip_model_name(esp_chip_model_t model)
{
    switch (model) {
        case CHIP_ESP32:    return "ESP32";
        case CHIP_ESP32S2:  return "ESP32-S2";
        case CHIP_ESP32S3:  return "ESP32-S3";
        case CHIP_ESP32C2:  return "ESP32-C2";
        case CHIP_ESP32C3:  return "ESP32-C3";
        case CHIP_ESP32C6:  return "ESP32-C6";
        case CHIP_ESP32H2:  return "ESP32-H2";
        case CHIP_ESP32P4:  return "ESP32-P4";
        default:            return "UNKNOWN";
    }
}

int sic_sysinfo(sic_sysinfo_t* out)
{
    if (!out) return SIC_EINVAL;

    memset(out, 0, sizeof(*out));

    esp_chip_info_t info;
    memset(&info, 0, sizeof(info));
    esp_chip_info(&info);

    out->chip_model = sic_chip_model_name(info.model);
    out->chip_rev   = (uint32_t)info.revision;
    out->cpu_mhz    = (uint32_t)(esp_clk_cpu_freq() / 1000000);

    uint32_t flash_size = 0;
    /* NULL selects the default (main) flash chip. */
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        out->flash_bytes = flash_size;
    }
    out->flash_hz = SIC_FLASH_HZ;

#if defined(CONFIG_SPIRAM)
    out->psram_bytes = esp_psram_is_initialized() ? (uint32_t)esp_psram_get_size() : 0u;
#else
    out->psram_bytes = 0u;
#endif

    /*
     * ESP_MAC_BASE, not ESP_MAC_WIFI_STA: the base MAC is read straight out of
     * eFuse and needs no peripheral or driver to be initialised, so it is a
     * valid device identifier on every target. The Wi-Fi-derived MAC types are
     * only registered in IDF's MAC table under SOC_WIFI_SUPPORTED, so asking
     * for ESP_MAC_WIFI_STA on a radio-less part (e.g. ESP32-P4) fails.
     */
    (void)esp_read_mac(out->mac, ESP_MAC_BASE);

    return SIC_OK;
}

#endif /* SIC_BACKEND_ESPIDF */
