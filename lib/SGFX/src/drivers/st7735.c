/* SGFX ST7735 driver (RGB565, streaming, no per-device state)
 * - Works with 80x160 "green-tab" style panels (T-Dongle-S3 etc.)
 * - Rotation via MADCTL (0..3)
 * - Offsets (COLSTART/ROWSTART) swap automatically on MV rotations
 * - Optional color order (BGR/RGB) and inversion
 */
#ifdef SGFX_DRV_ST7735

#include "sgfx.h"
#include "st77xx_common.h"
#include "sgfx_port.h"
#include <stdint.h>
#include <string.h>

/*---------------- Tunables via build flags ----------------*/
#ifndef SGFX_ST7735_COLSTART
#  define SGFX_ST7735_COLSTART  26   /* common for 160x80 "green tab" */
#endif
#ifndef SGFX_ST7735_ROWSTART
#  define SGFX_ST7735_ROWSTART  1
#endif
#ifndef SGFX_ST77XX_BGR
#  define SGFX_ST77XX_BGR       1    /* many ST77xx modules are BGR */
#endif
#ifndef SGFX_ST77XX_INVERT
#  define SGFX_ST77XX_INVERT    1    /* many 160x80 panels look right inverted */
#endif
#ifndef SGFX_ST7735_INIT_DELAY_MS
#  define SGFX_ST7735_INIT_DELAY_MS 120
#endif

#ifndef SGFX_BUS_SPI
#  error "ST7735 driver requires SPI: define SGFX_BUS_SPI"
#endif

/*---------------- ST77xx command set ----------------*/
#define ST77_CMD_SWRESET 0x01
#define ST77_CMD_SLPIN   0x10
#define ST77_CMD_SLPOUT  0x11
#define ST77_CMD_NORON   0x13
#define ST77_CMD_INVOFF  0x20
#define ST77_CMD_INVON   0x21
#define ST77_CMD_DISPOFF 0x28
#define ST77_CMD_DISPON  0x29
#define ST77_CMD_CASET   0x2A
#define ST77_CMD_RASET   0x2B
#define ST77_CMD_RAMWR   0x2C
#define ST77_CMD_MADCTL  0x36
#define ST77_CMD_COLMOD  0x3A
#define ST77_CMD_FRMCTR1 0xB1
#define ST77_CMD_FRMCTR2 0xB2
#define ST77_CMD_FRMCTR3 0xB3
#define ST77_CMD_INVCTR  0xB4
#define ST77_CMD_PWCTR1  0xC0
#define ST77_CMD_PWCTR2  0xC1
#define ST77_CMD_PWCTR3  0xC2
#define ST77_CMD_PWCTR4  0xC3
#define ST77_CMD_PWCTR5  0xC4
#define ST77_CMD_VMCTR1  0xC5
#define ST77_CMD_GMCTRP1 0xE0
#define ST77_CMD_GMCTRN1 0xE1

/* MADCTL bits */
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_BGR 0x08

/*---------------- Port helpers (from HAL) ----------------*/
#ifndef sgfx_cmd8
  extern int  sgfx_cmd8 (sgfx_device_t*, uint8_t cmd);
#endif
#ifndef sgfx_cmdn
  extern int  sgfx_cmdn (sgfx_device_t*, uint8_t cmd, const uint8_t* data, size_t n);
#endif
#ifndef sgfx_data
  extern int  sgfx_data (sgfx_device_t*, const void* bytes, size_t n);
#endif
#ifndef sgfx_delay_ms
  extern void sgfx_delay_ms(uint32_t ms);
#endif

/* Compute controller-offset swap for current rotation */
static inline void st7735_get_ofs(uint8_t rot, uint16_t* xofs, uint16_t* yofs){
  uint16_t cs = (uint16_t)SGFX_ST7735_COLSTART;
  uint16_t rs = (uint16_t)SGFX_ST7735_ROWSTART;
  if (rot & 1){ *xofs = rs; *yofs = cs; } else { *xofs = cs; *yofs = rs; }
}

/*---------------- Driver ops ----------------*/

static int st7735_set_rotation(sgfx_device_t* d, uint8_t rot){
  rot &= 3;
  uint8_t mad = 0;
  if (SGFX_ST77XX_BGR) mad |= MADCTL_BGR;

  /* These mappings match the typical 80x160 glass orientation */
  switch (rot){
    case 0: mad |= (MADCTL_MX | MADCTL_MY); break;  /* portrait 0° */
    case 1: mad |= (MADCTL_MY | MADCTL_MV); break;  /* landscape 90° */
    case 2: mad |= (0);                    break;   /* portrait 180° */
    case 3: mad |= (MADCTL_MX | MADCTL_MV); break;  /* landscape 270° */
  }
  return sgfx_cmdn(d, ST77_CMD_MADCTL, &mad, 1);
}

static int st7735_set_window(sgfx_device_t* d, int x,int y,int w,int h){
  uint16_t xo, yo; st7735_get_ofs(d->rotation & 3, &xo, &yo);
  uint16_t x0 = (uint16_t)x + xo,         y0 = (uint16_t)y + yo;
  uint16_t x1 = (uint16_t)(x+w-1) + xo,   y1 = (uint16_t)(y+h-1) + yo;

  uint8_t ca[4] = { (uint8_t)(x0>>8), (uint8_t)x0, (uint8_t)(x1>>8), (uint8_t)x1 };
  uint8_t ra[4] = { (uint8_t)(y0>>8), (uint8_t)y0, (uint8_t)(y1>>8), (uint8_t)y1 };

  int rc = sgfx_cmdn(d, ST77_CMD_CASET, ca, 4); if (rc) return rc;
  rc = sgfx_cmdn(d, ST77_CMD_RASET, ra, 4);     if (rc) return rc;
  rc = sgfx_cmd8(d, ST77_CMD_RAMWR);            if (rc) return rc;
  return SGFX_OK;
}

static int st7735_write_pixels(sgfx_device_t* d, const void* src, size_t count, sgfx_pixfmt_t fmt){
  if (fmt != SGFX_FMT_RGB565) return SGFX_ERR_NOSUP;
#ifdef SGFX_RGB565_BYTESWAP
  /* ESP32 stores uint16_t LE; display expects BE — swap each pixel's bytes */
#ifndef SGFX_ST7735_SWAP_BUF_BYTES
#  define SGFX_ST7735_SWAP_BUF_BYTES 4096
#endif
  static uint8_t swap_buf[SGFX_ST7735_SWAP_BUF_BYTES];
  const uint8_t* s = (const uint8_t*)src;
  size_t remaining = count;
  while (remaining) {
    size_t npx = remaining;
    size_t maxpx = sizeof(swap_buf) / 2;
    if (npx > maxpx) npx = maxpx;
    for (size_t i = 0; i < npx; ++i) {
      swap_buf[2*i]   = s[2*i+1];
      swap_buf[2*i+1] = s[2*i];
    }
    int r = sgfx_data(d, swap_buf, npx * 2);
    if (r) return r;
    s += npx * 2;
    remaining -= npx;
  }
  return 0;
#else
  return sgfx_data(d, src, count * 2);
#endif
}

static int st7735_fill_rect(sgfx_device_t* d, int x,int y,int w,int h, sgfx_rgba8_t c){
  /* pack RGB565, byte-swap if needed */
  uint16_t p = (uint16_t)(((c.r & 0xF8)<<8) | ((c.g & 0xFC)<<3) | (c.b>>3));
#ifdef SGFX_RGB565_BYTESWAP
  p = (uint16_t)((p << 8) | (p >> 8));
#endif
  int rc = st7735_set_window(d, x, y, w, h);
  if (rc) return rc;

  size_t total = (size_t)w * (size_t)h;
  size_t maxpx = d->scratch_bytes / 2;
  if (!maxpx) return SGFX_ERR_NOMEM;

  uint16_t* buf = (uint16_t*)d->scratch;
  for (size_t i=0;i<maxpx;++i) buf[i] = p;

  while (total){
    size_t n = (total > maxpx) ? maxpx : total;
    rc = sgfx_data(d, buf, n * 2);
    if (rc) return rc;
    total -= n;
  }
  return SGFX_OK;
}

static int st7735_present(sgfx_device_t* d){ (void)d; return SGFX_OK; }

/* Robust ST7735S init (power + gamma) for 80x160 class panels */
static int st7735_init(sgfx_device_t* d){
  int rc = sgfx_cmd8(d, ST77_CMD_SWRESET); if (rc) return rc;
  sgfx_delay_ms(5);

  rc = sgfx_cmd8(d, ST77_CMD_SLPOUT);      if (rc) return rc;
  sgfx_delay_ms(SGFX_ST7735_INIT_DELAY_MS);

  /* Frame rate control */
  { const uint8_t v[] = { 0x01, 0x2C, 0x2D }; rc = sgfx_cmdn(d, ST77_CMD_FRMCTR1, v, sizeof v); if (rc) return rc; }
  { const uint8_t v[] = { 0x01, 0x2C, 0x2D }; rc = sgfx_cmdn(d, ST77_CMD_FRMCTR2, v, sizeof v); if (rc) return rc; }
  { const uint8_t v[] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D }; rc = sgfx_cmdn(d, ST77_CMD_FRMCTR3, v, sizeof v); if (rc) return rc; }

  /* Inversion control (line inversion) */
  { const uint8_t v[] = { 0x07 }; rc = sgfx_cmdn(d, ST77_CMD_INVCTR, v, sizeof v); if (rc) return rc; }

  /* Power sequence */
  { const uint8_t v[] = { 0xA2, 0x02, 0x84 }; rc = sgfx_cmdn(d, ST77_CMD_PWCTR1, v, sizeof v); if (rc) return rc; }
  { const uint8_t v[] = { 0xC5 };             rc = sgfx_cmdn(d, ST77_CMD_PWCTR2, v, sizeof v); if (rc) return rc; }
  { const uint8_t v[] = { 0x0A, 0x00 };       rc = sgfx_cmdn(d, ST77_CMD_PWCTR3, v, sizeof v); if (rc) return rc; }
  { const uint8_t v[] = { 0x8A, 0x2A };       rc = sgfx_cmdn(d, ST77_CMD_PWCTR4, v, sizeof v); if (rc) return rc; }
  { const uint8_t v[] = { 0x8A, 0xEE };       rc = sgfx_cmdn(d, ST77_CMD_PWCTR5, v, sizeof v); if (rc) return rc; }
  { const uint8_t v[] = { 0x0E };             rc = sgfx_cmdn(d, ST77_CMD_VMCTR1, v, sizeof v); if (rc) return rc; }

  /* 16-bit color */
  { const uint8_t v[] = { 0x55 }; rc = sgfx_cmdn(d, ST77_CMD_COLMOD, v, sizeof v); if (rc) return rc; }

  /* Apply build-time rotation; also sets MADCTL color order */
  rc = st7735_set_rotation(d, d->rotation & 3); if (rc) return rc;

  /* Optional display inversion */
#if SGFX_ST77XX_INVERT
  rc = sgfx_cmd8(d, ST77_CMD_INVON);  if (rc) return rc;
#else
  rc = sgfx_cmd8(d, ST77_CMD_INVOFF); if (rc) return rc;
#endif

  /* Normal display on */
  rc = sgfx_cmd8(d, ST77_CMD_NORON);  if (rc) return rc; sgfx_delay_ms(10);
  rc = sgfx_cmd8(d, ST77_CMD_DISPON); if (rc) return rc; sgfx_delay_ms(10);
  return SGFX_OK;
}

/*---------------- Public symbols ----------------*/

const sgfx_caps_t sgfx_st7735_caps = {
  .width  = SGFX_W,
  .height = SGFX_H,
};

const sgfx_driver_ops_t sgfx_st7735_ops = {
  .init         = st7735_init,
  .set_window   = st7735_set_window,
  .write_pixels = st7735_write_pixels,
  .fill_rect    = st7735_fill_rect,
  .present      = st7735_present,
  .set_rotation = st7735_set_rotation,
};

#endif /* SGFX_DRV_ST7735 */
