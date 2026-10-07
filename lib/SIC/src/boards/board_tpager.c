#include "sic/sic_board.h"
#include "sic/input/kscan.h"
#include "sic/input/encoder.h"
#include "sic/audio/codec_es8311.h"
#include "sic/power/bq25896.h"
#include "sic/storage/sd.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/gpio_bus.h"
#include "sic/bus/delay.h"

/* Defined in src/drivers/input/tpager_keymap.c */
extern char sic_keymap_tpager(int idx);
extern char sic_keymap_tpager_alt(int idx);

/* Orange key is bit 20 (row 2, col 0) — held to access alt layer */
static const sic_kscan_cfg_t g_kbd_cfg = {
    .keymap        = sic_keymap_tpager,
    .keymap_alt    = sic_keymap_tpager_alt,
    .modifier_mask = (1ULL << 20),  /* Orange key (row 2 col 0) */
    .shift_mask    = (1ULL << 28),  /* Shift/CAP key (row 2 col 8) */
    .rows          = 4,
    .cols          = 10,
};

/*
 * I2C bus shared by TCA8418 keyboard, BQ27220 battery gauge, BQ25896 charger,
 * XL9555 GPIO expander, ES8311 audio codec, and other peripherals:
 *   SDA = GPIO 3, SCL = GPIO 2  (define I2C_SDA_PIN / I2C_SCL_PIN in build_flags)
 *
 * Battery:  BQ27220 fuel gauge — add -DSIC_BATTERY_BQ27220 to build_flags to
 *           activate the I2C-based sic_battery_read() implementation.
 */

/*
 * The TCA8418 keyboard controller is held in hardware reset by the XL9555
 * GPIO expander (I2C 0x20) until EXPANDS_KB_RST (P02, bit 2) and
 * EXPANDS_KB_EN (P10, bit 0 of port 1) are driven HIGH.  This must happen
 * before any I2C access to TCA8418 (0x34), otherwise every write NACKs.
 *
 * XL9555/PCA9555 register map used here:
 *   0x02 = OUTPUT_0  (P0x output latch; 0=LOW, 1=HIGH)
 *   0x03 = OUTPUT_1  (P1x output latch)
 *   0x06 = CONFIG_0  (P0x direction; 0=output, 1=input)
 *   0x07 = CONFIG_1  (P1x direction)
 */
static void tpager_gpio_high(int pin) {
    sic_gpio_mode(pin, 1);
    sic_gpio_write(pin, 1);
}

static void tpager_preinit(void) {
    uint8_t w[2];

    /* The T-Pager has a shared SPI bus.  Before any SPI peripheral is touched,
     * force every known chip-select high.  Otherwise an unowned display/NFC/LoRa
     * target may be selected while SD is mounting and corrupt the bus.
     */
    tpager_gpio_high(21); /* SD CS */
    tpager_gpio_high(36); /* LoRa CS */
    tpager_gpio_high(38); /* Display CS */
    tpager_gpio_high(39); /* NFC CS */
    sic_gpio_mode_pullup(33); /* shared MISO idle high helps SD-card init */

    /* XL9555/PCA9555 outputs.  Do not write 0xFF to both output ports: that
     * powers unrelated shared-SPI devices (LoRa/NFC/GNSS) and was enough to
     * make SD mount fail on some units.  Only enable the rails SIC actually
     * owns for the universal demo: speaker amp, keyboard reset/enable, SD.
     *   Port0: P01=Speaker, P02=Keyboard reset. Other power rails off/low.
     *   Port1: P10=Keyboard power, P14=SD power. P12 remains input SD-detect.
     */
    w[0] = 0x02; w[1] = 0x06;  sic_i2c_write(0, 0x20, w, 2); /* OUTPUT_0 */
    w[0] = 0x03; w[1] = 0x11;  sic_i2c_write(0, 0x20, w, 2); /* OUTPUT_1 */
    w[0] = 0x06; w[1] = 0x40;  sic_i2c_write(0, 0x20, w, 2); /* CONFIG_0: P0,1,2,3,4,5,7 outputs; P6 input/no-connect */
    w[0] = 0x07; w[1] = 0xEE;  sic_i2c_write(0, 0x20, w, 2); /* CONFIG_1: P10/P14 outputs; P12 SD detect input */
    sic_delay_ms(20);
}

/* Rotary encoder — GPIO 40=CLK/A, 41=DT/B, 7=SW (active-low push button) */
static const sic_encoder_cfg_t g_enc_cfg = {
    .pin_a   = 40,
    .pin_b   = 41,
    .pin_btn = 7,
};

/*
 * ES8311 audio codec (I2C 0x18 on shared bus, I2S on GPIO 10/11/18/45/17).
 * Both SIC_F_MIC and SIC_F_AMP point to the same cfg — the driver singleton
 * initialises the chip once and exposes separate mic_t / amp_t instances.
 */
static const sic_es8311_cfg_t g_es_cfg = {
    .i2c_bus  = 0,
    .i2c_addr = 0x18,
    .pin_mclk = 10,
    .pin_bclk = 11,
    .pin_ws   = 18,
    .pin_dout = 45,
    .pin_din  = 17,
};


/* BQ25896 charger / power-path controller on shared I2C bus (0x6B). */
static const sic_bq25896_cfg_t g_chg_cfg = {
    .i2c_bus  = 0,
    .i2c_addr = 0x6B,
};

/* microSD: CS=GPIO21, data/clock shared with board SPI bus.
 * SD power is enabled by XL9555 P14.  Other SPI CS lines are forced high in
 * tpager_preinit so SD owns the shared bus during mount.
 */
static const sic_sd_spi_cfg_t g_sd_cfg = {
    .cs_pin   = 21,
    .mosi_pin = 34,
    .sck_pin  = 35,
    .miso_pin = 33,
    .hz       = 4000000u,
    .power_i2c_bus     = 0,
    .power_i2c_addr    = 0x20,
    .power_output_reg  = 0x03, /* XL9555 OUTPUT_1, P14 = bit4 */
    .power_config_reg  = 0x07, /* XL9555 CONFIG_1, 0=output */
    .power_bit         = 4,
    .power_active_high = 1,
    .power_on_delay_ms = 300,
};

static const struct sic_board_ic_s g_ics[] = {
    { SIC_F_KSCAN,   "kbd_tca8418",  &g_kbd_cfg },
    { SIC_F_ENCODER, "encoder_gpio", &g_enc_cfg },
    { SIC_F_MIC,     "codec_es8311", &g_es_cfg  },
    { SIC_F_AMP,     "codec_es8311", &g_es_cfg  },
    { SIC_F_CHARGER, "bq25896",      &g_chg_cfg },
    { SIC_F_SD,      "sd_spi",       &g_sd_cfg  },
};

const struct sic_board_s SIC_BOARD_TPAGER = {
    .name     = "tpager",
    .ics      = g_ics,
    .ic_count = sizeof(g_ics) / sizeof(g_ics[0]),
    .preinit  = tpager_preinit,
};
