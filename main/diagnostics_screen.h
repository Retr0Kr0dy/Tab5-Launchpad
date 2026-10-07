#pragma once

#include "sgfx.h"
#include "sic/input/touch.h"

#ifdef __cplusplus
extern "C" {
#endif

struct konsole;

int run_diagnostics_screen(sgfx_device_t* d, const touch_t* t, struct konsole* ks);

#ifdef __cplusplus
}
#endif
