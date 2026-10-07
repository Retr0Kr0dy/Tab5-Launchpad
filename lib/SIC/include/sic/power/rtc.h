#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  sec, min, hour;   /* hour is 24h */
    uint8_t  mday, mon, wday;  /* mon is 1-12, wday is 0-6 (0=Sunday) */
    uint16_t year;             /* full 4-digit year, e.g. 2026 */
} sic_rtc_time_t;

struct rtc_vtbl_s {
    int (*get_time)(const void* self, sic_rtc_time_t* out);
    int (*set_time)(const void* self, const sic_rtc_time_t* in);
};
typedef struct rtc_s { const struct rtc_vtbl_s* v; void* impl; } rtc_t;

typedef struct sic_rtc_cfg_s { int i2c_bus; uint8_t i2c_addr; } sic_rtc_cfg_t;

const rtc_t* sic_rtc(int index);

#ifdef __cplusplus
}
#endif
