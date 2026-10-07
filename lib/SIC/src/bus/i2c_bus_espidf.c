/*
 * i2c_bus_espidf.c — ESP-IDF native I2C master backend.
 *
 * Implements the I2C half of the backend contract documented in
 * include/sic/sic_backend.h, on top of ESP-IDF v5's *new-style* driver
 * (driver/i2c_master.h), not the deprecated legacy driver/i2c.h.
 *
 * Board-agnostic: no pin numbers, no device addresses, no board assumptions.
 *
 * ── Two things this backend deliberately does differently from the Arduino
 *    backend (src/bus/i2c_bus_arduino.cpp) ────────────────────────────────
 *
 * 1. `bus` is a real index. The Arduino backend accepts a `bus` argument and
 *    then throws it away, always driving the single global `Wire` object, so
 *    boards with two genuinely independent I2C peripherals silently end up
 *    sharing one. Here each `bus` index owns its own i2c_master_bus_handle_t
 *    bound to i2c_port == bus, so two indices are two independent,
 *    simultaneously-usable controllers.
 *
 * 2. Device handles are cached, not required from callers. Arduino's
 *    Wire.beginTransmission(addr) addresses a slave per call; ESP-IDF's
 *    i2c_master.h instead requires a per-device i2c_master_dev_handle_t
 *    obtained once via i2c_master_bus_add_device(). SIC's contract passes a
 *    bare 7-bit address on every call with no registration step, so this file
 *    bridges the two models with a small fixed-size (bus,addr) -> handle
 *    cache, populated lazily on first use at the bus's configured clock.
 *    Bus scanning needs no device handle at all — i2c_master_probe() works
 *    straight off the bus handle.
 *
 * Return convention matches the rest of SIC: byte count (or 0) on success,
 * SIC_EIO (-5) on transfer failure, SIC_EINVAL (-3) on bad arguments.
 */
#if defined(SIC_BACKEND_ESPIDF)

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "soc/soc_caps.h"
#if defined(SOC_HP_I2C_NUM) && SOC_HP_I2C_NUM > 0
#include "soc/clk_tree_defs.h"
#endif

#include "sic/sic.h"
#include "sic/bus/i2c_bus.h"

/* Number of distinct bus indices this backend can hold open. Bounded by the
 * target's real controller count at runtime (i2c_new_master_bus() rejects an
 * out-of-range port), so this is only the size of the bookkeeping table. */
#ifndef SIC_I2C_MAX_BUS
#define SIC_I2C_MAX_BUS 4
#endif

/* Size of the lazily-populated (bus,addr) -> device-handle cache. */
#ifndef SIC_I2C_MAX_DEV
#define SIC_I2C_MAX_DEV 32
#endif

/* Per-transfer timeout. Generous enough for clock-stretching slaves at
 * 100 kHz, short enough that a dead bus fails fast instead of hanging. */
#ifndef SIC_I2C_XFER_TIMEOUT_MS
#define SIC_I2C_XFER_TIMEOUT_MS 100
#endif

/* Per-address timeout while scanning; a scan touches 126 addresses, so this
 * is kept well below the transfer timeout. */
#ifndef SIC_I2C_PROBE_TIMEOUT_MS
#define SIC_I2C_PROBE_TIMEOUT_MS 50
#endif

/* Used when a caller passes hz == 0 to sic_i2c_begin_bus(). */
#ifndef SIC_I2C_DEFAULT_HZ
#define SIC_I2C_DEFAULT_HZ 100000u
#endif

/* Standard glitch filter width, in I2C module clock cycles (IDF default). */
#define SIC_I2C_GLITCH_IGNORE_CNT 7

#define SIC_I2C_ADDR_MAX_7BIT 0x7Fu

static i2c_master_bus_handle_t g_bus[SIC_I2C_MAX_BUS];
static uint32_t                g_bus_hz[SIC_I2C_MAX_BUS];
static bool                    g_bus_ready[SIC_I2C_MAX_BUS];

typedef struct {
    i2c_master_dev_handle_t dev;
    int                     bus;
    uint8_t                 addr;
    bool                    used;
} sic_i2c_dev_slot_t;

static sic_i2c_dev_slot_t g_dev[SIC_I2C_MAX_DEV];

static bool sic_i2c_bus_index_ok(int bus)
{
    return (bus >= 0) && (bus < SIC_I2C_MAX_BUS);
}

/* Resolve — creating on first use — the device handle for (bus, addr).
 * Returns NULL if the bus was never begun, the cache is full, or the driver
 * refused to add the device. */
static i2c_master_dev_handle_t sic_i2c_dev_get(int bus, uint8_t addr)
{
    int free_slot = -1;

    if (!sic_i2c_bus_index_ok(bus) || !g_bus_ready[bus]) return NULL;

    for (int i = 0; i < SIC_I2C_MAX_DEV; i++) {
        if (g_dev[i].used) {
            if (g_dev[i].bus == bus && g_dev[i].addr == addr) return g_dev[i].dev;
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) return NULL; /* cache exhausted */

    i2c_device_config_t dcfg = {0};
    dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dcfg.device_address  = addr;
    dcfg.scl_speed_hz    = g_bus_hz[bus];

    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(g_bus[bus], &dcfg, &dev) != ESP_OK) return NULL;

    g_dev[free_slot].dev  = dev;
    g_dev[free_slot].bus  = bus;
    g_dev[free_slot].addr = addr;
    g_dev[free_slot].used = true;
    return dev;
}

int sic_i2c_begin_bus(int bus, int sda, int scl, uint32_t hz)
{
    if (!sic_i2c_bus_index_ok(bus)) return SIC_EINVAL;
    if (sda < 0 || scl < 0 || sda == scl) return SIC_EINVAL;

    /* Idempotent: re-begin on an already-open bus is a successful no-op, so
     * callers (board bring-up, individual drivers) need not coordinate. */
    if (g_bus_ready[bus]) return SIC_OK;

    i2c_master_bus_config_t cfg = {0};
    cfg.i2c_port          = (i2c_port_num_t)bus;
    cfg.sda_io_num        = (gpio_num_t)sda;
    cfg.scl_io_num        = (gpio_num_t)scl;
    cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
#if defined(SOC_HP_I2C_NUM) && SOC_HP_I2C_NUM > 0
    /* Chips with a split HP/LP I2C design (e.g. ESP32-P4) route any port
     * index >= SOC_HP_I2C_NUM to the low-power-domain LP_I2C controller,
     * which only accepts LP-domain clock sources (SOC_LP_I2C_CLKS) --
     * I2C_CLK_SRC_DEFAULT is an HP-domain clock and gets rejected with
     * "the clock source does not support lp i2c" if left as-is -- on Tab5
     * this affects the keyboard-accessory 2x4 GPIO header (GPIO0/GPIO1),
     * only reachable via LP_I2C on this chip. Other targets (no
     * SOC_HP_I2C_NUM at all, e.g. ESP32-S3) are unaffected -- this branch
     * doesn't even compile in for them. */
    if (bus >= SOC_HP_I2C_NUM) {
        cfg.clk_source = (i2c_clock_source_t)LP_I2C_SCLK_DEFAULT;
    }
#endif
    cfg.glitch_ignore_cnt = SIC_I2C_GLITCH_IGNORE_CNT;
    cfg.intr_priority     = 0; /* let the driver pick */
    cfg.trans_queue_depth = 0; /* synchronous transfers only */
    cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t handle = NULL;
    if (i2c_new_master_bus(&cfg, &handle) != ESP_OK) return SIC_EIO;

    g_bus[bus]       = handle;
    g_bus_hz[bus]    = hz ? hz : SIC_I2C_DEFAULT_HZ;
    g_bus_ready[bus] = true;
    return SIC_OK;
}

int sic_i2c_scan_bus(int bus, uint8_t* addrs, int max)
{
    if (!addrs || max <= 0) return SIC_EINVAL;
    if (!sic_i2c_bus_index_ok(bus)) return SIC_EINVAL;
    if (!g_bus_ready[bus]) return SIC_EIO;

    int n = 0;
    /* 0x00 (general call) and 0x7F upward are reserved; probe 0x01..0x7E. */
    for (uint8_t a = 1; a < SIC_I2C_ADDR_MAX_7BIT && n < max; a++) {
        if (i2c_master_probe(g_bus[bus], a, SIC_I2C_PROBE_TIMEOUT_MS) == ESP_OK) {
            addrs[n++] = a;
        }
    }
    return n;
}

int sic_i2c_probe_status(int bus, uint8_t addr)
{
    if (!sic_i2c_bus_index_ok(bus) || !g_bus_ready[bus]) return SIC_EIO;
    esp_err_t err = i2c_master_probe(g_bus[bus], addr, SIC_I2C_PROBE_TIMEOUT_MS);
    if (err == ESP_OK) return SIC_I2C_PROBE_ACK;
    if (err == ESP_ERR_TIMEOUT) return SIC_I2C_PROBE_STUCK;
    return SIC_I2C_PROBE_NACK;
}

int sic_i2c_write(int bus, uint8_t addr, const uint8_t* buf, int n)
{
    if (!buf || n < 0 || addr > SIC_I2C_ADDR_MAX_7BIT) return SIC_EINVAL;
    if (n == 0) return 0;

    i2c_master_dev_handle_t dev = sic_i2c_dev_get(bus, addr);
    if (!dev) return SIC_EIO;

    return (i2c_master_transmit(dev, buf, (size_t)n, SIC_I2C_XFER_TIMEOUT_MS) == ESP_OK)
               ? n : SIC_EIO;
}

int sic_i2c_read(int bus, uint8_t addr, uint8_t* buf, int n)
{
    if (!buf || n < 0 || addr > SIC_I2C_ADDR_MAX_7BIT) return SIC_EINVAL;
    if (n == 0) return 0;

    i2c_master_dev_handle_t dev = sic_i2c_dev_get(bus, addr);
    if (!dev) return SIC_EIO;

    return (i2c_master_receive(dev, buf, (size_t)n, SIC_I2C_XFER_TIMEOUT_MS) == ESP_OK)
               ? n : SIC_EIO;
}

int sic_i2c_writeread(int bus, uint8_t addr, const uint8_t* wr, int nw, uint8_t* rd, int nr)
{
    if (addr > SIC_I2C_ADDR_MAX_7BIT || nw < 0 || nr < 0) return SIC_EINVAL;
    if (nw > 0 && !wr) return SIC_EINVAL;
    if (nr > 0 && !rd) return SIC_EINVAL;

    /* Degenerate halves: i2c_master_transmit_receive() expects both a write
     * and a read phase, so route one-sided calls to the single-phase APIs. */
    if (nw == 0) return sic_i2c_read(bus, addr, rd, nr);
    if (nr == 0) return sic_i2c_write(bus, addr, wr, nw);

    i2c_master_dev_handle_t dev = sic_i2c_dev_get(bus, addr);
    if (!dev) return SIC_EIO;

    /* Single repeated-START transaction — the bus is never released between
     * the register write and the data read, so a second master (or another
     * SIC driver on the same bus) cannot interleave. */
    esp_err_t err = i2c_master_transmit_receive(dev, wr, (size_t)nw,
                                                rd, (size_t)nr,
                                                SIC_I2C_XFER_TIMEOUT_MS);
    return (err == ESP_OK) ? nr : SIC_EIO;
}

#endif /* SIC_BACKEND_ESPIDF */
