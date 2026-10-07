#pragma once
#include "sic.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const sic_board_t SIC_BOARD_CARDPUTER;
extern const sic_board_t SIC_BOARD_CARDPUTER_ADV;
extern const sic_board_t SIC_BOARD_HELTEC_V3;
extern const sic_board_t SIC_BOARD_TPAGER;
extern const sic_board_t SIC_BOARD_TAB5;
const sic_board_t* sic_board_default(void);

#ifdef __cplusplus
}
#endif
