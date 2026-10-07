#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int  sic_i2c_begin_bus(int bus, int sda, int scl, uint32_t hz);
int  sic_i2c_scan_bus(int bus, uint8_t* addrs, int max);

/* Single-address probe distinguishing ACK / NACK / stuck-bus, matching the
 * 3-way distinction M5Tab5-UserDemo's own bsp_i2c_scan() makes (m5stack_tab5.c)
 * but sic_i2c_scan_bus() collapses into a single "not found" bucket. A
 * timeout (SIC_I2C_PROBE_STUCK) means the bus never released -- usually a
 * powered-down/miswired slave holding a line low, not "nothing connected". */
#define SIC_I2C_PROBE_ACK   0
#define SIC_I2C_PROBE_NACK  1
#define SIC_I2C_PROBE_STUCK 2
int  sic_i2c_probe_status(int bus, uint8_t addr);
int  sic_i2c_write(int bus, uint8_t addr, const uint8_t* buf, int n);
int  sic_i2c_read (int bus, uint8_t addr, uint8_t* buf, int n);
int  sic_i2c_writeread(int bus, uint8_t addr, const uint8_t* wr, int nw, uint8_t* rd, int nr);
#ifdef __cplusplus
}
#endif
