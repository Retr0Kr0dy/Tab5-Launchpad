#pragma once
#ifdef __cplusplus
extern "C" {
#endif
struct pwr_sw_vtbl_s { void (*set)(const void*, int on); int (*power_good)(const void*); };
typedef struct pwr_sw_s { const struct pwr_sw_vtbl_s* v; void* impl; } pwr_sw_t;
const pwr_sw_t* sic_pwr_sw(int index);
#ifdef __cplusplus
}
#endif
