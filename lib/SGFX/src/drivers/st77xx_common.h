#pragma once
#include "sgfx.h"
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08
#define MADCTL_MH  0x04

static inline uint8_t st77xx_madctl_for(uint8_t rot, int bgr){
  static const uint8_t map[4] = {
    (MADCTL_MX | 0),
    (MADCTL_MV | 0),
    (MADCTL_MY | 0),
    (MADCTL_MX | MADCTL_MY | MADCTL_MV),
  };
  uint8_t m = map[rot & 3];
  if (bgr) m |= MADCTL_BGR; else m |= MADCTL_RGB;
  return m;
}
typedef struct { uint16_t x, y; } xy_t;
static inline xy_t st77xx_offsets_for(uint8_t rot, uint16_t colstart, uint16_t rowstart){
  switch (rot & 3){
    case 0: return (xy_t){ colstart, rowstart };
    case 1: return (xy_t){ rowstart, colstart };
    case 2: return (xy_t){ colstart, rowstart };
    default:return (xy_t){ rowstart, colstart };
  }
}
