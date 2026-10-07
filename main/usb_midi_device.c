/*
 * usb_midi_device.c — see usb_midi_device.h for the always-on rationale.
 *
 * Built on the managed `espressif/esp_tinyusb` component (not raw
 * `espressif/tinyusb`), with the same MIDI descriptor shape as ESP-IDF's
 * own official example (examples/peripherals/usb/device/tusb_midi), which
 * explicitly lists ESP32-P4 as a supported target -- real evidence this
 * controller works in device mode on this exact chip, not just a theory.
 * High-speed (480Mbit) descriptor is included alongside full-speed since
 * the P4's controller is HS-capable; esp_tinyusb negotiates whichever
 * speed the host actually grants.
 *
 * esp_tinyusb 2.x owns tud_mount_cb()/tud_umount_cb()/tud_suspend_cb()/
 * tud_resume_cb() itself (tinyusb.c) and re-dispatches them through a
 * single tinyusb_config_t.event_cb instead of the raw TinyUSB weak
 * callbacks the official example defines directly -- confirmed by reading
 * the actual fetched component source, not assumed from the example.
 */

#include "usb_midi_device.h"

#include <string.h>

#include "esp_log.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "usb-midi";

/* ── TinyUSB descriptors, lifted from ESP-IDF's own tusb_midi example ──── */

enum {
#if CFG_TUD_MIDI
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
#endif
    ITF_COUNT
};

enum {
    EP_EMPTY = 0,
#if CFG_TUD_MIDI
    EPNUM_MIDI,
#endif
};

#define TUSB_DESCRIPTOR_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_MIDI * TUD_MIDI_DESC_LEN)

static const char *s_str_desc[5] = {
    (char[]){0x09, 0x04},   /* 0: language ID, English (0x0409) */
    "Tab5-Launchpad",       /* 1: manufacturer */
    "Launchpad",            /* 2: product -- shown as the MIDI device name in a DAW */
    "0001",                 /* 3: serial -- not chip-unique */
    "USB-MIDI",              /* 4: MIDI interface name */
};

static const uint8_t s_midi_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 64),
};

#if (TUD_OPT_HIGH_SPEED)
static const uint8_t s_midi_hs_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 512),
};
#endif

static bool s_ready;
static volatile bool s_host_connected;

static void tinyusb_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_host_connected = true;
        ESP_LOGI(TAG, "host enumerated us");
        break;
    case TINYUSB_EVENT_DETACHED:
        s_host_connected = false;
        ESP_LOGI(TAG, "host unmounted us");
        break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        s_host_connected = false;
        break;
#endif
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        s_host_connected = true;
        break;
#endif
    default:
        break;
    }
}

esp_err_t orion_usb_midi_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.event_cb = tinyusb_event_cb;
    tusb_cfg.descriptor.string = s_str_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_str_desc) / sizeof(s_str_desc[0]);
    tusb_cfg.descriptor.full_speed_config = s_midi_cfg_desc;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = s_midi_hs_cfg_desc;
    tusb_cfg.descriptor.qualifier = NULL;
#endif

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "USB-MIDI device active");
    return ESP_OK;
}

bool orion_usb_midi_ready(void)
{
    return s_ready;
}

bool orion_usb_midi_host_connected(void)
{
    return s_host_connected;
}

int orion_usb_midi_send(const uint8_t packet3[3])
{
    if (!s_ready || !tud_midi_mounted()) {
        return -1;
    }
    uint32_t wrote = tud_midi_stream_write(0, packet3, 3);
    return (wrote == 3) ? 0 : -1;
}

int orion_usb_midi_available(void)
{
    if (!s_ready) {
        return 0;
    }
    return (int)tud_midi_available();
}

int orion_usb_midi_recv(uint8_t out3[3])
{
    if (!s_ready) {
        return -1;
    }
    uint8_t packet[4];
    if (!tud_midi_packet_read(packet)) {
        return -1;
    }
    /* packet[0] is the USB-MIDI event packet's own Cable Number + Code
     * Index Number header; packet[1..3] are the up-to-3 real MIDI bytes. */
    memcpy(out3, packet + 1, 3);
    return 0;
}
