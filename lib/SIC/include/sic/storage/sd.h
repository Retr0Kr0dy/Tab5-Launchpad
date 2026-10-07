#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct sic_sd_spi_cfg_s {
    int cs_pin;
    int mosi_pin;
    int sck_pin;
    int miso_pin;
    uint32_t hz; /* 0 = backend default */

    /* Optional board power sequencing.  This keeps SD socket power/control
     * inside the board profile instead of leaking it into examples.
     * For XL9555/PCA9555-style expanders, set power_i2c_addr plus the
     * output/config register and bit below.  Leave power_i2c_addr=0 when
     * the socket is always powered.
     */
    int     power_i2c_bus;
    uint8_t power_i2c_addr;
    uint8_t power_output_reg;
    uint8_t power_config_reg;
    uint8_t power_bit;
    uint8_t power_active_high;
    uint16_t power_on_delay_ms;
} sic_sd_spi_cfg_t;

struct sd_vtbl_s {
    int      (*begin)(const void* self);
    int      (*present)(const void* self);
    uint64_t (*card_size_bytes)(const void* self);
};

typedef struct sd_s {
    const struct sd_vtbl_s* v;
    void* impl;
} sd_t;

const sd_t* sic_sd(int index);

#ifdef __cplusplus
}
#endif
