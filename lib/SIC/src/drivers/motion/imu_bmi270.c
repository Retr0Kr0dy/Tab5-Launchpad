/*
 * imu_bmi270.c — Bosch BMI270 accel+gyro (I2C, addr 0x68, internal bus).
 *
 * The device address is 0x68 (SDO tied low on this board) -- 0x69
 * (SDO=high) NACKs every transaction on real hardware.
 *
 * A Bosch config-file upload is required for basic operation, not an
 * optional accuracy nicety: without it, accel/gyro registers read a clean,
 * unmoving +0 on every axis. Per Bosch's own BMI270 datasheet/app notes,
 * the chip's internal feature engine gates the ADCs entirely until its
 * config file is loaded and BMI2_INTERNAL_STATUS confirms
 * BMI2_CONFIG_LOAD_SUCCESS. bmi270_load_config() below is a direct port of
 * Bosch's own write_config_file()/upload_file()/set_config_load() (bmi2.c)
 * -- same register sequence (INIT_CTRL/INIT_ADDR_0/INIT_ADDR_1/INIT_DATA),
 * same chunked burst-write approach, same INTERNAL_STATUS poll — with the
 * config *data* (bmi270_config_data.h, 8192 bytes) reformatted from
 * M5Unified's own vendored copy of Bosch's public, BSD-licensed
 * BMI270_config.inl into plain C99 (same bytes, not re-derived). One
 * deliberate deviation from the reference: Bosch's sequence re-enables
 * advanced power-save (APS) after the config load completes, which then
 * requires per-transaction settle delays (~450us, gated on `aps_status`)
 * that this driver's I2C helpers have no equivalent for — so APS is left
 * disabled instead (matching the rest of this driver's already-working init
 * flow), which the datasheet ties to power consumption during idle periods,
 * not to config-load correctness, so it should not affect data validity.
 *
 * ============================================================================
 * SCOPE WARNING — READ BEFORE EXTENDING THIS DRIVER
 * ============================================================================
 * This is deliberately a *narrow* driver: fixed range/ODR, raw accelerometer
 * and gyroscope registers only, now WITH Bosch's config blob loaded (see
 * above) so those registers actually produce data. Still no feature engine
 * beyond what the config file itself enables by default, no step counter,
 * no gesture/wrist-wear detection, no self-calibration routine, no
 * BMI270_SensorAPI dependency (the upload sequence and blob are both
 * standalone, not the Bosch library).
 * ============================================================================
 *
 * Register addresses below are taken from Bosch's own public BMI2 register
 * map (cross-checked against the reference component's
 * sensor_bmi270/include/bmi2_defs.h, which is the vendored copy of Bosch's
 * BMI270_SensorAPI headers — addresses only, no code copied):
 *   0x00 CHIP_ID_ADDR        (expect 0x24 for BMI270)
 *   0x0C ACC_X_LSB_ADDR      (6 bytes: ACC_X, ACC_Y, ACC_Z, each LSB+MSB)
 *   0x12 GYR_X_LSB_ADDR      (6 bytes: GYR_X, GYR_Y, GYR_Z, each LSB+MSB)
 *   0x40 ACC_CONF_ADDR, 0x41 ACC_RANGE (adjacent registers, per Bosch's own
 *        register map comments about writing/reading them together)
 *   0x42 GYR_CONF_ADDR, 0x43 GYR_RANGE (same adjacency convention)
 *   0x7C PWR_CONF_ADDR       (bit0 adv_power_save)
 *   0x7D PWR_CTRL_ADDR       (bit1 gyr_en, bit2 acc_en)
 *   0x7E CMD_REG_ADDR        (0xB6 = soft reset)
 *
 * Fixed configuration chosen to match M5Stack's own reference firmware's
 * defaults for this exact board (accel_gyro_bmi270_enable_sensor() in the
 * mined component): accelerometer +/-4g, gyroscope +/-1000dps, both at
 * 200Hz ODR, normal bandwidth, performance-optimized filter.
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only
 * sic_i2c_* and sic_delay_ms from the SIC HAL, no Arduino/ESP-IDF headers,
 * no Bosch BMI270_SensorAPI dependency. probe() does no I/O — init (soft
 * reset, chip-ID check, ODR/range/power config) is deferred to the first
 * read() call, per DESIGN_INVARIANTS.md.
 */
#include <stdint.h>
#include <string.h>
#include "sic/sic_registry.h"
#include "sic/motion/imu.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/delay.h"
#include "bmi270_config_data.h"

#define BMI270_REG_CHIP_ID   0x00u
#define BMI270_REG_INTERNAL_STATUS 0x21u
#define BMI270_REG_ACC_X_LSB 0x0Cu
#define BMI270_REG_GYR_X_LSB 0x12u
#define BMI270_REG_ACC_CONF  0x40u
#define BMI270_REG_ACC_RANGE 0x41u
#define BMI270_REG_GYR_CONF  0x42u
#define BMI270_REG_GYR_RANGE 0x43u
#define BMI270_REG_INIT_CTRL  0x59u
#define BMI270_REG_INIT_ADDR0 0x5Bu  /* 2-byte write covers 0x5B+0x5C */
#define BMI270_REG_INIT_DATA  0x5Eu
#define BMI270_REG_PWR_CONF  0x7Cu
#define BMI270_REG_PWR_CTRL  0x7Du
#define BMI270_REG_CMD       0x7Eu

#define BMI270_INTERNAL_STATUS_LOAD_MASK 0x0Fu
#define BMI270_INTERNAL_STATUS_LOAD_OK   0x01u

#define BMI270_CHIP_ID_VAL   0x24u
#define BMI270_CMD_SOFTRESET 0xB6u

#define BMI270_ACC_RANGE_4G     0x01u
#define BMI270_GYR_RANGE_1000   0x01u
#define BMI270_ODR_200HZ        0x09u
#define BMI270_BWP_NORMAL       0x02u
#define BMI270_FILTER_PERF_BIT  0x80u /* bit 7 */
#define BMI270_NOISE_PERF_BIT   0x40u /* bit 6, GYR_CONF only */

#define BMI270_PWR_CTRL_GYR_EN  0x02u /* bit 1 */
#define BMI270_PWR_CTRL_ACC_EN  0x04u /* bit 2 */

/* Full-scale ranges matching the fixed configuration above, used to scale
 * raw 16-bit signed samples: value = raw * range / 32768.0f. */
#define BMI270_ACC_RANGE_G      4.0f
#define BMI270_GYR_RANGE_DPS    1000.0f

typedef struct {
    sic_imu_cfg_t cfg;
    int           init_done;
    int           init_failed;
} bmi270_ctx_t;

static bmi270_ctx_t g_ctx;
static imu_t         g_imu;

static int bmi270_read(const bmi270_ctx_t* c, uint8_t reg, uint8_t* buf, int len) {
    return sic_i2c_writeread(c->cfg.i2c_bus, c->cfg.i2c_addr, &reg, 1, buf, len) == len ? 0 : -1;
}

static int bmi270_write8(const bmi270_ctx_t* c, uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return sic_i2c_write(c->cfg.i2c_bus, c->cfg.i2c_addr, b, 2) == 2 ? 0 : -1;
}

/* reg followed by `len` data bytes in one transaction -- used both for the
 * 2-byte INIT_ADDR_0/1 word-address write and for INIT_DATA config chunks.
 * 32 is comfortably above the 30-byte chunk size this driver uses. */
static int bmi270_write_burst(const bmi270_ctx_t* c, uint8_t reg, const uint8_t* data, int len) {
    uint8_t b[33];
    if (len < 0 || len > 32) return -1;
    b[0] = reg;
    memcpy(b + 1, data, (size_t)len);
    return sic_i2c_write(c->cfg.i2c_bus, c->cfg.i2c_addr, b, len + 1) == len + 1 ? 0 : -1;
}

/* Port of Bosch's write_config_file()/upload_file()/set_config_load()
 * (bmi2.c): disable config-load, burst-write BMI270_CONFIG_FILE in 30-byte
 * chunks (each preceded by a 2-byte word-address write to INIT_ADDR_0/1,
 * the on-chip data pointer -- I2C doesn't auto-increment the *register*
 * address across chunks, the chip's own internal pointer does), enable
 * config-load, then poll INTERNAL_STATUS for BMI2_CONFIG_LOAD_SUCCESS.
 * Called with advanced power-save already disabled (bmi270_init() does
 * that immediately before this). */
static int bmi270_load_config(const bmi270_ctx_t* c) {
    if (bmi270_write8(c, BMI270_REG_INIT_CTRL, 0x00u) != 0) return -1;   /* disable config load */

    enum { CHUNK = 30 };
    for (size_t index = 0; index < sizeof(BMI270_CONFIG_FILE); index += CHUNK) {
        size_t remain = sizeof(BMI270_CONFIG_FILE) - index;
        int    n      = remain < CHUNK ? (int)remain : CHUNK;

        uint16_t word_addr = (uint16_t)(index / 2);
        uint8_t  addr[2]   = { (uint8_t)(word_addr & 0x0Fu), (uint8_t)(word_addr >> 4) };
        if (bmi270_write_burst(c, BMI270_REG_INIT_ADDR0, addr, 2) != 0) return -1;
        if (bmi270_write_burst(c, BMI270_REG_INIT_DATA, BMI270_CONFIG_FILE + index, n) != 0) return -1;
    }

    if (bmi270_write8(c, BMI270_REG_INIT_CTRL, 0x01u) != 0) return -1;   /* enable config load */
    sic_delay_ms(20);   /* Bosch's own BMI2_INTERNAL_STATUS_READ_DELAY_MS */

    uint8_t status = 0;
    if (bmi270_read(c, BMI270_REG_INTERNAL_STATUS, &status, 1) != 0) return -1;
    if ((status & BMI270_INTERNAL_STATUS_LOAD_MASK) != BMI270_INTERNAL_STATUS_LOAD_OK) return -1;
    return 0;
}

/* Minimal, non-Bosch-API init: chip-ID check, soft reset, fixed range/ODR,
 * power sensors on. See the scope warning at the top of this file for what
 * this deliberately does not do.
 *
 * Two sequencing details matter here: (1) chip-ID must be read BEFORE the
 * soft-reset, matching Bosch's own bmi2_sec_init() (bmi2.c) exactly -- it
 * never re-reads CHIP_ID right after a reset, and that specific read NAKs
 * on real hardware if you do. (2) Bosch's datasheet-cited 2ms post-reset
 * settle time is not enough on this unit -- the next transaction after
 * soft-reset NAKed at 2ms; a 10ms margin resolved it. */
static int bmi270_init(bmi270_ctx_t* c) {
    uint8_t chip_id = 0;
    if (bmi270_read(c, BMI270_REG_CHIP_ID, &chip_id, 1) != 0) return -1;
    if (chip_id != BMI270_CHIP_ID_VAL) return -1;

    if (bmi270_write8(c, BMI270_REG_CMD, BMI270_CMD_SOFTRESET) != 0) return -1;
    sic_delay_ms(10);

    /* Advanced power-save must be off while writing sensor configuration
     * and while uploading the config file (Bosch's write_config_file()
     * requires the same). */
    if (bmi270_write8(c, BMI270_REG_PWR_CONF, 0x00u) != 0) return -1;
    sic_delay_ms(1);

    if (bmi270_load_config(c) != 0) return -1;

    uint8_t acc_conf = (uint8_t)(BMI270_FILTER_PERF_BIT | (BMI270_BWP_NORMAL << 4) | BMI270_ODR_200HZ);
    uint8_t gyr_conf = (uint8_t)(BMI270_FILTER_PERF_BIT | BMI270_NOISE_PERF_BIT |
                                  (BMI270_BWP_NORMAL << 4) | BMI270_ODR_200HZ);

    if (bmi270_write8(c, BMI270_REG_ACC_CONF, acc_conf) != 0) return -1;
    if (bmi270_write8(c, BMI270_REG_ACC_RANGE, BMI270_ACC_RANGE_4G) != 0) return -1;
    if (bmi270_write8(c, BMI270_REG_GYR_CONF, gyr_conf) != 0) return -1;
    if (bmi270_write8(c, BMI270_REG_GYR_RANGE, BMI270_GYR_RANGE_1000) != 0) return -1;

    if (bmi270_write8(c, BMI270_REG_PWR_CTRL,
                       (uint8_t)(BMI270_PWR_CTRL_ACC_EN | BMI270_PWR_CTRL_GYR_EN)) != 0) return -1;
    sic_delay_ms(2); /* let the sensors settle before the first data read */

    return 0;
}

static int16_t le16(const uint8_t* p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static int imu_read(const void* self, sic_imu_sample_t* out) {
    const imu_t* t = (const imu_t*)self;
    bmi270_ctx_t* c = t ? (bmi270_ctx_t*)t->impl : NULL;
    if (!c || !out) return -1;

    if (!c->init_done) {
        if (c->init_failed) return -1;
        int irc = bmi270_init(c);
        if (irc != 0) { c->init_failed = 1; return irc; }
        c->init_done = 1;
    }

    uint8_t acc[6], gyr[6];
    if (bmi270_read(c, BMI270_REG_ACC_X_LSB, acc, 6) != 0) return -1;
    if (bmi270_read(c, BMI270_REG_GYR_X_LSB, gyr, 6) != 0) return -1;

    out->ax = (float)le16(&acc[0]) * BMI270_ACC_RANGE_G / 32768.0f;
    out->ay = (float)le16(&acc[2]) * BMI270_ACC_RANGE_G / 32768.0f;
    out->az = (float)le16(&acc[4]) * BMI270_ACC_RANGE_G / 32768.0f;

    out->gx = (float)le16(&gyr[0]) * BMI270_GYR_RANGE_DPS / 32768.0f;
    out->gy = (float)le16(&gyr[2]) * BMI270_GYR_RANGE_DPS / 32768.0f;
    out->gz = (float)le16(&gyr[4]) * BMI270_GYR_RANGE_DPS / 32768.0f;

    return 0;
}

static const struct imu_vtbl_s BMI270_VT = { imu_read };

static int probe_imu_bmi270(const void* icdesc, void** out) {
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "bmi270") != 0 || !d->cfg) return -1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_imu_cfg_t*)d->cfg;
    g_imu.v    = &BMI270_VT;
    g_imu.impl = &g_ctx;
    *out = &g_imu;
    return 0;
}

static const sic_driver_t DRV_IMU_BMI270 = { "bmi270", SIC_F_IMU, probe_imu_bmi270, NULL };

void sic_register_driver_imu_bmi270(void) {
    sic_registry_register(&DRV_IMU_BMI270);
}
