#pragma once
#include "sic.h"
#ifdef __cplusplus
extern "C" {
#endif
void  sic_registry_register(const sic_driver_t* drv);
int   sic_registry_add(sic_func_id_t fn, const char* name, void* inst);
int   sic_count_fn(sic_func_id_t fn);
const char* sic_name_fn(sic_func_id_t fn, int idx);
void* sic_get_fn(sic_func_id_t fn, int idx);
/* Legacy probe entry */
int sic_begin_legacy(const sic_board_t* board, const sic_begin_opts_t* opts);
#ifdef __cplusplus
}
#endif
