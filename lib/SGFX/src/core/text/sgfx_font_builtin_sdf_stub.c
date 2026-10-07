// sgfx_font_builtin_sdf_stub.c
// Provides the symbols sgfx_text.c expects for the embedded SDF font pack.
// Replace with a real packed SDF blob later (xxd -i or your packer).

#include <stdint.h>

// Weak default: empty blob. sgfx_font_open_builtin() should gracefully fail and
// your app can fall back to another font (e.g., sgfx_font5x7) or external load.
const unsigned char _sgfx_builtin_sdf[] = { 0x00 };
const unsigned int  _sgfx_builtin_sdf_len = 0;
