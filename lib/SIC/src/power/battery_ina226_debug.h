/*
 * battery_ina226_debug.h — raw INA226 register dump, for diagnosing
 * charger-status issues directly on hardware: exposes every register
 * sic_battery_read()'s computed voltage/current numbers are actually built
 * from, so a diagnostic screen can show raw register values changing live
 * while reproducing a report.
 *
 * Deliberately NOT part of the generic sic/power/battery.h public API --
 * this is INA226-specific raw register access, not a portable battery
 * abstraction other backends could implement. Included the same way
 * "boards/tab5/ioexpander.h" already is: a board/backend-private header
 * under src/, reached via SIC_DIR/src (already an include dir), not the
 * public sic/ namespace.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t reg_config;
    uint16_t reg_shunt_voltage;
    uint16_t reg_bus_voltage;
    uint16_t reg_power;
    uint16_t reg_current;
    uint16_t reg_calibration;
    uint16_t reg_mask_enable;
    uint16_t reg_alert_limit;
    int read_errors;   /* count of the 8 registers above that failed to read */
    int cal_done;       /* g_cal_done -- calibration register write succeeded at least once */
    float voltage_v;    /* same formula sic_battery_read() uses, from reg_bus_voltage */
    float current_ma;   /* same formula sic_battery_read() uses, from reg_current (signed) */
} sic_ina226_debug_t;

/* Returns 0 always (partial reads are reported via read_errors/out fields
 * being left 0, not a hard failure) so a diagnostic screen can show
 * whatever it did get rather than an all-or-nothing dump. */
int sic_battery_ina226_debug_read(sic_ina226_debug_t* out);

#ifdef __cplusplus
}
#endif
