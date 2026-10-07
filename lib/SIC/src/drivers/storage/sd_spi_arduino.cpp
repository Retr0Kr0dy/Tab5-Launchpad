#if defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)
#define SIC_AUDIO_NO_COMPAT 1
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
extern "C" {
  #include "sic/sic.h"
  #include "sic/storage/sd.h"
  #include "sic/sic_registry.h"
  #include "sic/bus/i2c_bus.h"
  #include "sic/bus/delay.h"
}

typedef struct {
    sic_sd_spi_cfg_t cfg;
    int attempted;
    int mounted;
    int powered;
    uint32_t mounted_hz;
} sd_spi_ctx_t;

static sd_spi_ctx_t g_ctx;
static sd_t g_sd;
#if defined(FSPI)
static SPIClass g_sd_spi(FSPI);
#elif defined(VSPI)
static SPIClass g_sd_spi(VSPI);
#else
static SPIClass g_sd_spi;
#endif

static int sd_i2c_read8(int bus, uint8_t addr, uint8_t reg, uint8_t* out) {
    if (!out) return -1;
    return sic_i2c_writeread(bus, addr, &reg, 1, out, 1) == 1 ? 0 : -1;
}

static int sd_i2c_write8(int bus, uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return sic_i2c_write(bus, addr, b, 2) == 2 ? 0 : -1;
}

static int sd_spi_power_on(sd_spi_ctx_t* c) {
    if (!c) return -1;
    if (c->powered) return 0;
    if (c->cfg.power_i2c_addr) {
        uint8_t mask = (uint8_t)(1u << (c->cfg.power_bit & 7u));
        uint8_t outv = 0;
        uint8_t cfgv = 0;

        if (sd_i2c_read8(c->cfg.power_i2c_bus, c->cfg.power_i2c_addr,
                         c->cfg.power_output_reg, &outv) != 0) return -1;
        if (sd_i2c_read8(c->cfg.power_i2c_bus, c->cfg.power_i2c_addr,
                         c->cfg.power_config_reg, &cfgv) != 0) return -1;

        /* Make the power-control bit an output before the active edge, then do
         * a short off->on cycle.  This recovers cards that saw SPI edges before
         * their socket rail was valid, without leaking board-specific power
         * handling into the example.
         */
        cfgv &= (uint8_t)~mask; /* 0 = output on XL9555/PCA9555 */
        if (sd_i2c_write8(c->cfg.power_i2c_bus, c->cfg.power_i2c_addr,
                          c->cfg.power_config_reg, cfgv) != 0) return -1;

        if (c->cfg.power_active_high) outv &= (uint8_t)~mask;
        else                          outv |= mask;
        (void)sd_i2c_write8(c->cfg.power_i2c_bus, c->cfg.power_i2c_addr,
                            c->cfg.power_output_reg, outv);
        sic_delay_ms(30);

        if (c->cfg.power_active_high) outv |= mask;
        else                          outv &= (uint8_t)~mask;
        if (sd_i2c_write8(c->cfg.power_i2c_bus, c->cfg.power_i2c_addr,
                          c->cfg.power_output_reg, outv) != 0) return -1;
    }

    c->powered = 1;
    unsigned delay_ms = c->cfg.power_on_delay_ms ? c->cfg.power_on_delay_ms : 100u;
    sic_delay_ms(delay_ms);
    return 0;
}

static int sd_try_begin(sd_spi_ctx_t* c, uint32_t hz) {
    if (!c) return -1;
    SD.end();
    pinMode(c->cfg.cs_pin, OUTPUT);
    digitalWrite(c->cfg.cs_pin, HIGH);
    pinMode(c->cfg.miso_pin, INPUT_PULLUP);
    delay(10);

    g_sd_spi.begin(c->cfg.sck_pin, c->cfg.miso_pin, c->cfg.mosi_pin, c->cfg.cs_pin);
    delay(10);
    if (!SD.begin(c->cfg.cs_pin, g_sd_spi, hz)) {
        SD.end();
        return -1;
    }
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        return -2;
    }
    c->mounted_hz = hz;
    return 0;
}

static int sd_spi_begin(const void* self) {
    sd_spi_ctx_t* c = (sd_spi_ctx_t*)((const sd_t*)self)->impl;
    if (!c) return -1;
    if (c->mounted) return 0;
    c->attempted = 1;

    if (sd_spi_power_on(c) != 0) return -1;

    /* Start conservative.  Some board sockets sit behind power switches or
     * share an SPI bus; a 25 MHz first mount can fail with ESP32's
     * "physical drive cannot work" error on otherwise valid FAT/FAT32 cards.
     */
    const uint32_t preferred = c->cfg.hz ? c->cfg.hz : 4000000u;
    const uint32_t tries[] = { 400000u, 1000000u, preferred, 4000000u, 8000000u };
    uint32_t last = 0;
    for (unsigned i = 0; i < sizeof(tries) / sizeof(tries[0]); ++i) {
        uint32_t hz = tries[i];
        if (hz == 0 || hz == last) continue;
        last = hz;
        if (sd_try_begin(c, hz) == 0) {
            c->mounted = 1;
            return 0;
        }
        delay(25);
    }

    c->mounted = 0;
    return -1;
}

static int sd_spi_present(const void* self) {
    const sd_t* sd = (const sd_t*)self;
    sd_spi_ctx_t* c = sd ? (sd_spi_ctx_t*)sd->impl : nullptr;
    if (!c) return 0;
    if (!c->mounted && sd_spi_begin(self) != 0) return 0;
    return SD.cardType() != CARD_NONE;
}

static uint64_t sd_spi_card_size(const void* self) {
    if (!sd_spi_present(self)) return 0;
    return SD.cardSize();
}

static const struct sd_vtbl_s SD_SPI_VT = {
    sd_spi_begin,
    sd_spi_present,
    sd_spi_card_size
};

static int probe_sd_spi(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "sd_spi") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_sd_spi_cfg_t*)d->cfg;
    if (g_ctx.cfg.cs_pin < 0 || g_ctx.cfg.mosi_pin < 0 ||
        g_ctx.cfg.sck_pin < 0 || g_ctx.cfg.miso_pin < 0) return -1;
    g_sd.v = &SD_SPI_VT;
    g_sd.impl = &g_ctx;
    *out = &g_sd;
    return 0;
}

static const sic_driver_t DRV_SD_SPI = { "sd_spi", SIC_F_SD, probe_sd_spi, NULL };

extern "C" void sic_register_driver_sd_spi(void) {
    sic_registry_register(&DRV_SD_SPI);
}
#endif
