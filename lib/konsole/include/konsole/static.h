#ifndef KONSOLE_STATIC_H
#define KONSOLE_STATIC_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "konsole.h"
struct kon_line_state {
    char   line[KONSOLE_MAX_LINE];
    size_t len;
    size_t cursor;
#if KONSOLE_HISTORY > 0
    char   hist[KONSOLE_HISTORY][KONSOLE_MAX_LINE];
    int    hist_count;
    int    hist_head;
    int    hist_nav;
#else
    int    hist_count;
    int    hist_head;
    int    hist_nav;
#endif
};
#endif
