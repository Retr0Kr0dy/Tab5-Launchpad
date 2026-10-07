#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int sic_mychip_begin(void);
int sic_mychip_read(int *value);
#ifdef __cplusplus
}
#endif
