#include <stddef.h>
#include "sic/sic_board.h"

/*
 * Build-target selected default board.
 *
 * Keep target-selection preprocessor logic inside SIC, not in examples/apps.
 * Application code should call sic_board_default() and let PlatformIO build
 * flags choose the hardware profile.
 */
const sic_board_t* sic_board_default(void) {
#if defined(SIC_TARGET_CARDPUTER_ADV)
    return &SIC_BOARD_CARDPUTER_ADV;
#elif defined(SIC_TARGET_CARDPUTER)
    return &SIC_BOARD_CARDPUTER;
#elif defined(SIC_TARGET_TPAGER)
    return &SIC_BOARD_TPAGER;
#elif defined(SIC_TARGET_HELTEC_V3)
    return &SIC_BOARD_HELTEC_V3;
#elif defined(SIC_TARGET_TAB5)
    return &SIC_BOARD_TAB5;
#else
    return NULL;
#endif
}
