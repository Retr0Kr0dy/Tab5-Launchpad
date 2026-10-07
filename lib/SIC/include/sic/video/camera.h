/*
 * camera.h — generic SIC camera capture contract (SIC_F_CAMERA).
 *
 * Deliberately vendor-neutral: nothing from ESP-IDF's esp_video /
 * esp_cam_sensor components (V4L2 ioctl structs, esp_cam_sensor_* types,
 * device-node paths, pixel-format enums) appears here, so the component
 * behind it stays swappable -- the *implementation* may depend on esp_video,
 * but the SIC-facing API stays clean.
 *
 * Frame lifecycle is borrow-and-return, not copy: get_frame() hands back a
 * pointer into a driver-owned buffer that stays valid only until the matching
 * release_frame() call. Callers that need the pixels past that point must
 * copy them. Frame *interpretation* (fourcc decoding, colour conversion,
 * JPEG, scaling) is entirely the caller's job — SIC is a transport only.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Requested capture format. `fourcc` is a four-character code in the usual
 * little-endian packing ('R' | 'G'<<8 | 'B'<<16 | 'P'<<24 for RGB565), the
 * same numeric convention V4L2 uses. A driver may report the format it
 * actually settled on differing from the request; width/height of 0 mean
 * "keep the sensor's default", and a fourcc of 0 means "keep the driver's
 * default".
 */
typedef struct { uint16_t width, height; uint32_t fourcc; } sic_cam_format_t;

struct camera_vtbl_s {
    /*
     * Power up the sensor, configure `fmt`, and begin streaming. 0 on
     * success. `fmt` is in/out: width/height/fourcc of 0 on input mean
     * "keep the current default", and on return the driver overwrites them
     * with whatever it actually negotiated -- callers that passed all
     * zeros (the common case) have no other way to learn the real
     * resolution/pixel format they are about to receive from get_frame().
     */
    int (*start)(const void* self, sic_cam_format_t* fmt);
    /* Stop streaming, release buffers and power the sensor back down. */
    int (*stop)(const void* self);
    /*
     * Dequeue the next captured frame. On success, the values written through
     * buf and len describe a driver-owned buffer that stays valid only until
     * the matching release_frame() call, and the value written through seq is a
     * monotonically increasing frame counter. Any of buf/len/seq may be NULL
     * if the caller does not want that value. `timeout_ms` is a best-effort
     * upper bound on the wait; a backend whose underlying dequeue primitive
     * has no timeout may block longer (documented per driver).
     */
    int (*get_frame)(const void* self, void** buf, size_t* len, uint32_t* seq, int timeout_ms);
    /* Return a buffer previously handed out by get_frame() to the driver. */
    int (*release_frame)(const void* self, void* buf);
};
typedef struct camera_s { const struct camera_vtbl_s* v; void* impl; } camera_t;

/*
 * Board-supplied configuration. Deliberately has no reset-pin field, for the
 * same reason sic_touch_cfg_t does not: on Tab5 the sensor's reset line is a
 * bit on a shared I2C GPIO expander, not a GPIO a generic cfg struct could
 * carry, and the driver asserts/releases it itself inside start()/stop().
 */
typedef struct sic_camera_cfg_s {
    int pin_clk;       /* camera XCLK GPIO, driven by LEDC (Tab5: GPIO36); <0 = externally clocked */
    uint32_t clk_hz;   /* XCLK frequency (Tab5: 24000000) */
} sic_camera_cfg_t;

const camera_t* sic_camera(int index);

#ifdef __cplusplus
}
#endif
