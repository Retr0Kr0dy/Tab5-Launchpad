/*
 * battery_bq27220.c — sic_battery_read() via TI BQ27220 fuel gauge (I2C).
 *
 * Activated when -DSIC_BATTERY_BQ27220 is set in build_flags.
 * The stub in battery_adc.cpp is suppressed automatically in that case.
 *
 * Build flags:
 *   SIC_BATTERY_BQ27220     — must be defined to compile this file's implementation
 *   BQ27220_I2C_BUS         — SIC I2C bus index (default 0)
 *   BQ27220_I2C_ADDR        — 7-bit I2C address (default 0x55)
 */

#ifdef SIC_BATTERY_BQ27220

#include <stdint.h>
#include "sic/bus/i2c_bus.h"
#include "sic/power/battery.h"
#include "sic/sic.h"

#ifndef BQ27220_I2C_BUS
#  define BQ27220_I2C_BUS  0
#endif
#ifndef BQ27220_I2C_ADDR
#  define BQ27220_I2C_ADDR 0x55
#endif

/* BQ27220 standard command registers (little-endian uint16) */
#define BQ27220_REG_VOLTAGE 0x08  /* Voltage(), mV */
#define BQ27220_REG_SOC     0x1C  /* StateOfCharge(), % (0-100) */

static int read_u16_le(uint8_t reg, uint16_t* out) {
    uint8_t buf[2] = {0, 0};
    int rc = sic_i2c_writeread(BQ27220_I2C_BUS, BQ27220_I2C_ADDR,
                               &reg, 1, buf, 2);
    if (rc < 0) return rc;
    *out = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    return 0;
}

int sic_battery_read(sic_battery_t* out) {
    if (!out) return SIC_EINVAL;
    uint16_t mv = 0, soc = 0;
    if (read_u16_le(BQ27220_REG_VOLTAGE, &mv)  < 0) return SIC_EIO;
    if (read_u16_le(BQ27220_REG_SOC,     &soc) < 0) return SIC_EIO;
    out->voltage_v = (float)mv / 1000.0f;
    out->percent   = (int)soc;
    if (out->percent < 0)   out->percent = 0;
    if (out->percent > 100) out->percent = 100;
    out->current_ma = 0.0f;   /* not read by this driver; see sic/power/battery.h */
    return 0;
}

#endif /* SIC_BATTERY_BQ27220 */
