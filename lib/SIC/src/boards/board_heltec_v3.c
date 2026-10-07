#include <stddef.h>
#include "sic/sic.h"
#include "sic/sic_board.h"
static const struct sic_board_ic_s g_ics[] = {
  { SIC_F_KSCAN, "dummy", NULL }
};
const struct sic_board_s SIC_BOARD_HELTEC_V3 = {
  .name = "heltec_v3",
  .ics = g_ics,
  .ic_count = sizeof(g_ics)/sizeof(g_ics[0])
};
