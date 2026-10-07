#include "sic/sic_board.h"
#include "sic/input/kscan.h"
#include "sic/audio/mic_pdm.h"
#include "sic/audio/i2s_amp.h"
#include "sic/audio/codec_es8311.h"
#include "sic/ir/ir_tx.h"
#include "sic/storage/sd.h"

/* Defined in src/drivers/input/cardputer_keymap.c */
extern char sic_keymap_cardputer(int idx);
extern sic_keycode_t sic_keycode_cardputer(int idx);
extern sic_keycode_t sic_keycode_cardputer_fn(int idx);
extern int sic_cardputer_tca8418_scan_to_index(int scan_code);

/* Logical modifier bits in the Cardputer 4x14 keymap:
 *   Fn=row2/col0 idx2, Shift=row2/col1 idx6,
 *   Ctrl=row3/col0 idx3, Opt=row3/col1 idx7, Alt=row3/col2 idx11.
 */
#define CP_BIT(idx) (1ULL << (idx))

static const sic_kscan_cfg_t g_cardputer_kbd_cfg = {
    .keymap     = sic_keymap_cardputer,
    .keycode    = sic_keycode_cardputer,
    .keycode_fn = sic_keycode_cardputer_fn,
    .fn_mask    = CP_BIT(2),
    .shift_mask = CP_BIT(6),
    .ctrl_mask  = CP_BIT(3),
    .opt_mask   = CP_BIT(7),
    .modifier_mask = CP_BIT(11), /* Alt */
    .rows = 8,
    .cols = 7,
};


/* Cardputer original/v1.1 audio pin map from the official M5Stack PinMap:
 *   SPM1423 PDM mic: DAT=G46, CLK=G43
 *   NS4168 speaker:  BCLK=G41, SDATA=G42, LRCLK=G43
 */
static const sic_pdm_mic_cfg_t g_cardputer_pdm_mic_cfg = {
    .clk_pin    = 43,
    .data_pin   = 46,
    .right_slot = 1,
};

static const sic_i2s_amp_cfg_t g_cardputer_ns4168_cfg = {
    .bclk_pin = 41,
    .ws_pin   = 43,
    .dout_pin = 42,
};


static const sic_ir_tx_cfg_t g_cardputer_ir_cfg = {
    .pin = 44,
    .carrier_hz = 38000,
    .active_high = 1,
};

static const sic_sd_spi_cfg_t g_cardputer_sd_cfg = {
    .cs_pin   = 12,
    .mosi_pin = 14,
    .sck_pin  = 40,
    .miso_pin = 39,
    .hz       = 4000000u,
};

/* Cardputer-ADV audio pin map from official M5Stack PinMap:
 *   ES8311 I2C:  SDA=G8, SCL=G9
 *   I2S: BCLK/SCLK=G41, LRCK=G43, ASDOUT(codec->ESP)=G46, DSDIN(ESP->codec)=G42
 * M5's public pin map does not expose a dedicated MCLK GPIO for ADV, so SIC
 * leaves mck_io_num as I2S_PIN_NO_CHANGE (-1) and drives the standard I2S
 * clocks.  This makes ADV caps honest and keeps any future MCLK/new-I2S fix
 * confined to the ES8311 backend, not to applications.
 */
static const sic_es8311_cfg_t g_cardputer_adv_es_cfg = {
    .i2c_bus  = 0,
    .i2c_addr = 0x18,
    .pin_mclk = -1,
    .pin_bclk = 41,
    .pin_ws   = 43,
    .pin_dout = 42,
    .pin_din  = 46,
    .clock_from_bclk = 1,
};

static const sic_kscan_cfg_t g_cardputer_adv_kbd_cfg = {
    .keymap        = sic_keymap_cardputer,
    .keycode       = sic_keycode_cardputer,
    .keycode_fn    = sic_keycode_cardputer_fn,
    .fn_mask       = CP_BIT(2),
    .shift_mask    = CP_BIT(6),
    .ctrl_mask     = CP_BIT(3),
    .opt_mask      = CP_BIT(7),
    .modifier_mask = CP_BIT(11), /* Alt */
    .scan_to_index = sic_cardputer_tca8418_scan_to_index,
    /* ADV TCA8418 electrical matrix is 7 rows x 8 columns.  Do not transpose:
     * the scan normalizer above converts that raw matrix to the 4x14 logical
     * Cardputer keymap. */
    .rows          = 7,
    .cols          = 8,
};

static const struct sic_board_ic_s g_cardputer_ics[] = {
    { SIC_F_KSCAN,   "kbd_74hc138", &g_cardputer_kbd_cfg      },
    { SIC_F_MIC,     "mic_pdm",     &g_cardputer_pdm_mic_cfg  },
    { SIC_F_AMP,     "ns4168",      &g_cardputer_ns4168_cfg   },
    { SIC_F_CHARGER, "tp4057",      NULL                      },
    { SIC_F_IR_TX,   "ir_gpio",     &g_cardputer_ir_cfg       },
    { SIC_F_SD,      "sd_spi",      &g_cardputer_sd_cfg       },
};

static const struct sic_board_ic_s g_cardputer_adv_ics[] = {
    { SIC_F_KSCAN, "kbd_tca8418",  &g_cardputer_adv_kbd_cfg },
    { SIC_F_MIC,   "codec_es8311", &g_cardputer_adv_es_cfg  },
    { SIC_F_AMP,   "codec_es8311", &g_cardputer_adv_es_cfg  },
    { SIC_F_IR_TX, "ir_gpio",      &g_cardputer_ir_cfg      },
    { SIC_F_SD,    "sd_spi",       &g_cardputer_sd_cfg      },
};

const struct sic_board_s SIC_BOARD_CARDPUTER = {
    .name     = "cardputer",
    .ics      = g_cardputer_ics,
    .ic_count = sizeof(g_cardputer_ics) / sizeof(g_cardputer_ics[0])
};

const struct sic_board_s SIC_BOARD_CARDPUTER_ADV = {
    .name     = "cardputer-adv",
    .ics      = g_cardputer_adv_ics,
    .ic_count = sizeof(g_cardputer_adv_ics) / sizeof(g_cardputer_adv_ics[0])
};
