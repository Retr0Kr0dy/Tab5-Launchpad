/* kscan_dispatch.c — single TU that owns the global kscan_read_bitmap symbol. */
#include "sic/input/kscan.h"

int kscan_read_bitmap(const struct kscan_s* self, unsigned long long* out_bitmap) {
    if (!self || !self->v || !self->v->read_bitmap || !out_bitmap) return -1;
    return self->v->read_bitmap(self, out_bitmap);
}
