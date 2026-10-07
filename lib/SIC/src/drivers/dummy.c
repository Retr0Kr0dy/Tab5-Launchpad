#include "sic/sic.h"
#include <stdlib.h>
#include "sic/sic_registry.h"
#include "sic/input/kscan.h"
#include "sic/power/switch.h"
#include "sic/power/charger.h"
#include "sic/audio/mic.h"
#include "sic/audio/amp.h"
#include "sic/storage/sd.h"
#include "sic/ir/ir_tx.h"

static int  kbd_rk (const void* self){ (void)self; return -1; }
static int  kbd_rb (const struct kscan_s* self, unsigned long long* out){ (void)self; *out=0; return 0; }
static const struct kscan_vtbl_s KBDVT = { kbd_rk, kbd_rb };
static kscan_t KBD = { .v = &KBDVT, .impl = NULL, .keymap = NULL };

static void sw_set(const void* self, int on){ (void)self;(void)on; }
static int  sw_pg (const void* self){ (void)self; return 1; }
static const struct pwr_sw_vtbl_s SWVT = { sw_set, sw_pg };
static pwr_sw_t SW = { &SWVT, NULL };

static int chg_state(const void* self){ (void)self; return SIC_CHG_NOT_PRESENT; }
static const struct charger_vtbl_s CHGVT = { chg_state };
static charger_t CHG = { &CHGVT, NULL };

static int mic_start(const void* self, int hz){ (void)self;(void)hz; return 0; }
static int mic_read (const void* self, short* b, int n){ (void)self; for(int i=0;i<n;i++) b[i]=0; return n; }
static const struct mic_vtbl_s MICVT = { mic_start, mic_read };
static mic_t MIC = { &MICVT, NULL };

static void amp_enable(const void* self, int on){ (void)self;(void)on; }
static const struct amp_vtbl_s AMPVT = { .enable = amp_enable };
static amp_t AMP = { &AMPVT, NULL };

static int ir_send(const void* self, uint32_t code){ (void)self;(void)code; return 0; }
static const struct ir_vtbl_s IRVT = { ir_send };
static ir_t IR = { &IRVT, NULL };

static sd_t SD = {0};

static int probe_dummy(const void* ic, void** out){
  (void)ic;
  *out = &KBD;  sic_registry_add(SIC_F_PWR_SW,"sy7088",&SW);
  sic_registry_add(SIC_F_AMP,"ns4168",&AMP);
  sic_registry_add(SIC_F_MIC,"mic_pdm",&MIC);
  sic_registry_add(SIC_F_CHARGER,"tp4057",&CHG);
  sic_registry_add(SIC_F_IR_TX,"ir_rmt",&IR);
  sic_registry_add(SIC_F_SD,"sd_spi",&SD);
  return 0;
}
static const sic_driver_t DRV_DUMMY = { .name="dummy", .function=SIC_F_KSCAN, .probe=probe_dummy, .remove=NULL };
#ifdef __cplusplus
extern "C" { void sic_register_driver_dummy(void){ sic_registry_register(&DRV_DUMMY); } }
#else
void sic_register_driver_dummy(void){ sic_registry_register(&DRV_DUMMY); }
#endif
