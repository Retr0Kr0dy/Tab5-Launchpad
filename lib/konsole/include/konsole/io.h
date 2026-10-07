#ifndef KONSOLE_IO_H
#define KONSOLE_IO_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
struct konsole_io {
    size_t   (*read_avail)(void *ctx);
    size_t   (*read)(void *ctx, uint8_t *buf, size_t len);
    size_t   (*write)(void *ctx, const uint8_t *buf, size_t len);
    uint32_t (*millis)(void *ctx);
    void *ctx;
};
#endif
