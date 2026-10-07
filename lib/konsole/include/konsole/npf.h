#ifndef NPF_H
#define NPF_H
#include <stdarg.h>
#include <stddef.h>
int npf_vsnprintf(char *buffer, size_t bufsz, const char *format, va_list vlist);
#endif
