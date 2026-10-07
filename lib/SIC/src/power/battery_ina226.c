/*
 * battery_ina226.c — sic_battery_read() via the Tab5 INA226 power monitor.
 *
 * The INA226 is a current/voltage monitor, not the board's charger. Charging
 * is handled by the IP2326; see charger_tab5_ioexp.c. The monitor is still
 * useful for battery voltage, current direction, diagnostics, and the
 * documented placeholder state-of-charge estimate below.
 *
 * Register/calibration values are cross-checked against M5Stack's Tab5
 * reference: 5 mOhm shunt, 8.192 A range -> Current_LSB=250 uA and CAL=4096.
 * On this board's physical shunt orientation the INA226 raw current is
 * negative while current flows into the pack and positive while the pack is
 * discharging. SIC's public battery API intentionally uses the friendlier
 * convention positive=charging, negative=discharging, so the raw register is
 * multiplied by -1 here. Callers therefore do not need board-specific sign
 * knowledge.
 *
 * PERCENT-MAPPING CAVEAT: the linear 2S Li-Po approximation below
 * (6.0 V=0%, 8.4 V=100%) is a clearly-labelled placeholder pending real
 * discharge-curve calibration. It is not a fuel gauge and must not be
 * presented as an accurate state-of-charge measurement.
 */
#ifdef SIC_BATTERY_INA226

#include <stdint.h>
#include <string.h>
#include "sic/bus/i2c_bus.h"
#include "sic/power/battery.h"
#include "sic/sic.h"
#include "battery_ina226_debug.h"

#ifndef INA226_I2C_BUS
#  define INA226_I2C_BUS  0
#endif
#ifndef INA226_I2C_ADDR
#  define INA226_I2C_ADDR 0x41
#endif

#define INA226_REG_CONFIG     0x00u
#define INA226_REG_BUSVOLTAGE 0x02u
#define INA226_REG_CURRENT    0x04u
#define INA226_REG_CAL        0x05u
#define INA226_BUSVOLTAGE_LSB_MV 1.25f
#define INA226_CURRENT_LSB_MA    0.25f   /* 250uA, see the CAL derivation above */
#define INA226_CAL_VALUE         4096u

#ifndef INA226_CURRENT_CHARGE_SIGN
#  define INA226_CURRENT_CHARGE_SIGN -1  /* normalize Tab5 raw polarity to SIC: +charge / -discharge */
#endif

/* PLACEHOLDER 2S Li-Po linear range — see caveat above. */
#define INA226_PLACEHOLDER_EMPTY_V 6.0f
#define INA226_PLACEHOLDER_FULL_V  8.4f

static int g_cal_done = 0;

static int read_u16_be(uint8_t reg, uint16_t* out) {
    uint8_t buf[2] = {0, 0};
    int rc = sic_i2c_writeread(INA226_I2C_BUS, INA226_I2C_ADDR, &reg, 1, buf, 2);
    if (rc < 0) return rc;
    /* INA226 registers are big-endian on the wire (MSB first). */
    *out = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return 0;
}

static int write_u16_be(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFFu) };
    return (sic_i2c_write(INA226_I2C_BUS, INA226_I2C_ADDR, buf, 3) == 3) ? 0 : -1;
}

/* Lazy, once-only: write the Calibration register so Current/Power start
 * producing real values. Config register is left at its power-on default
 * (0x4127 per the datasheet, which is already continuous shunt+bus mode) —
 * only Calibration actually needs writing for this. */
static void ina226_ensure_cal(void) {
    if (g_cal_done) return;
    if (write_u16_be(INA226_REG_CAL, INA226_CAL_VALUE) == 0) g_cal_done = 1;
}

static int percent_from_voltage(float v) {
    if (v <= INA226_PLACEHOLDER_EMPTY_V) return 0;
    if (v >= INA226_PLACEHOLDER_FULL_V)  return 100;
    return (int)((v - INA226_PLACEHOLDER_EMPTY_V) *
                 (100.0f / (INA226_PLACEHOLDER_FULL_V - INA226_PLACEHOLDER_EMPTY_V)));
}

int sic_battery_read(sic_battery_t* out) {
    if (!out) return SIC_EINVAL;
    uint16_t raw = 0;
    if (read_u16_be(INA226_REG_BUSVOLTAGE, &raw) < 0) return SIC_EIO;

    out->voltage_v = (float)raw * INA226_BUSVOLTAGE_LSB_MV / 1000.0f;
    out->percent   = percent_from_voltage(out->voltage_v);
    if (out->percent < 0)   out->percent = 0;
    if (out->percent > 100) out->percent = 100;

    ina226_ensure_cal();
    uint16_t craw = 0;
    if (g_cal_done && read_u16_be(INA226_REG_CURRENT, &craw) == 0) {
        out->current_ma = (float)(int16_t)craw * INA226_CURRENT_LSB_MA
                          * (float)INA226_CURRENT_CHARGE_SIGN;
    } else {
        out->current_ma = 0.0f;
    }
    return 0;
}

#define INA226_REG_SHUNTVOLTAGE 0x01u
#define INA226_REG_POWER        0x03u
#define INA226_REG_MASKENABLE   0x06u
#define INA226_REG_ALERTLIMIT   0x07u

int sic_battery_ina226_debug_read(sic_ina226_debug_t* out) {
    if (!out) return SIC_EINVAL;
    memset(out, 0, sizeof *out);

    ina226_ensure_cal();
    out->cal_done = g_cal_done;

    uint16_t v;
    if (read_u16_be(INA226_REG_CONFIG, &v) == 0)        out->reg_config = v;        else out->read_errors++;
    if (read_u16_be(INA226_REG_SHUNTVOLTAGE, &v) == 0)  out->reg_shunt_voltage = v; else out->read_errors++;
    if (read_u16_be(INA226_REG_BUSVOLTAGE, &v) == 0)    out->reg_bus_voltage = v;   else out->read_errors++;
    if (read_u16_be(INA226_REG_POWER, &v) == 0)         out->reg_power = v;         else out->read_errors++;
    if (read_u16_be(INA226_REG_CURRENT, &v) == 0)       out->reg_current = v;       else out->read_errors++;
    if (read_u16_be(INA226_REG_CAL, &v) == 0)           out->reg_calibration = v;   else out->read_errors++;
    if (read_u16_be(INA226_REG_MASKENABLE, &v) == 0)    out->reg_mask_enable = v;   else out->read_errors++;
    if (read_u16_be(INA226_REG_ALERTLIMIT, &v) == 0)    out->reg_alert_limit = v;   else out->read_errors++;

    out->voltage_v = (float)out->reg_bus_voltage * INA226_BUSVOLTAGE_LSB_MV / 1000.0f;
    out->current_ma = (float)(int16_t)out->reg_current * INA226_CURRENT_LSB_MA
                      * (float)INA226_CURRENT_CHARGE_SIGN;
    return 0;
}

#endif /* SIC_BATTERY_INA226 */
