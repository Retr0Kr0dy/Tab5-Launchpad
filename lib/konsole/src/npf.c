#include "npf.h"
#include <stdio.h>

int npf_vsnprintf(char *b, size_t n, const char *f, va_list ap) {
    return vsnprintf(b,n,f,ap);
}
