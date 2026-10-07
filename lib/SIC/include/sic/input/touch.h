#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint16_t x, y; uint8_t id; uint8_t pressed; } sic_touch_point_t;

struct touch_vtbl_s {
    int (*read_points)(const void* self, sic_touch_point_t* out, int max_points); /* Current contact snapshot (0..max_points), <0 on error. No new hardware report preserves the last snapshot; a fresh empty report releases it. */
};
typedef struct touch_s { const struct touch_vtbl_s* v; void* impl; } touch_t;

typedef struct sic_touch_cfg_s {
    int      i2c_bus;
    uint8_t  i2c_addr;
    int      pin_int;   /* SIC_NOPIN if polled, no interrupt line wired */
    uint16_t max_x, max_y;
} sic_touch_cfg_t;

const touch_t* sic_touch(int index);

#ifdef __cplusplus
}
#endif
