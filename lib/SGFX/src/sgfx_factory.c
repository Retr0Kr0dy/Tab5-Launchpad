#include "sgfx.h"
#include "sgfx_hal.h"
#include <string.h>
#include <stdlib.h>

// HAL makers provided elsewhere
int sgfx_open_spi(sgfx_device_t* dev,
                  const sgfx_hal_cfg_spi_t* bus_cfg,
                  const sgfx_driver_ops_t* drv,
                  const void* drv_cfg)
{
  if (!dev || !drv || !bus_cfg) return SGFX_ERR_INVAL;
  memset(dev, 0, sizeof(*dev));
  dev->bus = (sgfx_bus_t*)calloc(1, sizeof(*dev->bus));
  int rc = sgfx_hal_make_spi(dev->bus, bus_cfg);
  if (rc) { SGFX_LOG("[SGFX] SPI HAL make failed: %d\n", rc); return rc; }
  dev->drv = drv;
  rc = dev->drv->init(dev);
  if (rc) { SGFX_LOG("[SGFX] drv init failed: %d\n", rc); return rc; }
#ifdef SGFX_DEFAULT_ROTATION
  sgfx_set_rotation(dev, (uint8_t)SGFX_DEFAULT_ROTATION);
#endif
#ifdef SGFX_DEFAULT_INVERT
  if (SGFX_DEFAULT_INVERT) sgfx_invert(dev, true);
#endif
  return 0;
}

int sgfx_open_dsi(sgfx_device_t* dev,
                  const sgfx_hal_cfg_dsi_t* bus_cfg,
                  const sgfx_driver_ops_t* drv,
                  const void* drv_cfg)
{
  if (!dev || !drv || !bus_cfg) return SGFX_ERR_INVAL;
  memset(dev, 0, sizeof(*dev));
  dev->bus = (sgfx_bus_t*)calloc(1, sizeof(*dev->bus));
  if (!dev->bus) return SGFX_ERR_NOMEM;
  int rc = sgfx_hal_make_dsi(dev->bus, bus_cfg);
  if (rc) { SGFX_LOG("[SGFX] DSI HAL make failed: %d\n", rc); return rc; }
  dev->drv = drv;
  rc = dev->drv->init(dev);
  if (rc) { SGFX_LOG("[SGFX] drv init failed: %d\n", rc); return rc; }
#ifdef SGFX_DEFAULT_ROTATION
  sgfx_set_rotation(dev, (uint8_t)SGFX_DEFAULT_ROTATION);
#endif
#ifdef SGFX_DEFAULT_INVERT
  /* Parity with sgfx_open_spi()'s SGFX_DEFAULT_INVERT block, but routed through
   * the driver op that actually exists. sgfx_open_spi() calls sgfx_invert(),
   * which is declared and defined nowhere in this repo — that block only builds
   * because SGFX_DEFAULT_INVERT is never defined by any current target. Left
   * as-is over there (out of scope for this phase); not replicated here. */
  if (SGFX_DEFAULT_INVERT && dev->drv->invert) dev->drv->invert(dev, true);
#endif
  return 0;
}

int sgfx_open_i2c(sgfx_device_t* dev,
                  const sgfx_hal_cfg_i2c_t* bus_cfg,
                  const sgfx_driver_ops_t* drv,
                  const void* drv_cfg)
{
  if (!dev || !drv || !bus_cfg) return SGFX_ERR_INVAL;
  memset(dev, 0, sizeof(*dev));
  dev->bus = (sgfx_bus_t*)calloc(1, sizeof(*dev->bus));
  int rc = sgfx_hal_make_i2c(dev->bus, bus_cfg);
  if (rc) { SGFX_LOG("[SGFX] I2C HAL make failed: %d\n", rc); return rc; }
  dev->drv = drv;
  rc = dev->drv->init(dev);
  if (rc) { SGFX_LOG("[SGFX] drv init failed: %d\n", rc); return rc; }
#ifdef SGFX_DEFAULT_ROTATION
  sgfx_set_rotation(dev, (uint8_t)SGFX_DEFAULT_ROTATION);
#endif
  return 0;
}
