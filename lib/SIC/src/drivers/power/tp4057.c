#include <string.h>
#include <stddef.h>
#include "sic/sic_registry.h"
#include "sic/power/charger.h"
#include "sic/bus/gpio_bus.h"
#include "sic/sic.h"

#ifndef PIN_TP4057_CHRG
#define PIN_TP4057_CHRG SIC_NOPIN
#endif
#ifndef PIN_TP4057_DONE
#define PIN_TP4057_DONE SIC_NOPIN
#endif

typedef struct { int chrg, done; } tp4057_t;

static int get_state(const void* self){
  const tp4057_t* s = (const tp4057_t*)self;
  if (s->chrg==SIC_NOPIN && s->done==SIC_NOPIN) return SIC_CHG_NOT_PRESENT;
  int chrg = (s->chrg==SIC_NOPIN)? 1 : sic_gpio_read(s->chrg); // active low
  int done = (s->done==SIC_NOPIN)? 1 : sic_gpio_read(s->done); // active low
  if (chrg==0){ return SIC_CHG_CHARGING; }
  if (done==0){ return SIC_CHG_FULL; }
  return SIC_CHG_NOT_PRESENT;
}
static const struct charger_vtbl_s VT = { get_state };
static tp4057_t    TP  = { PIN_TP4057_CHRG, PIN_TP4057_DONE };
static charger_t   CHG = { &VT, &TP };

static int probe(const void* ic, void** out){
  const sic_board_ic_t* d = (const sic_board_ic_t*)ic; if (!d||!d->hint) return -1;
  if (strcmp(d->hint,"tp4057")!=0) return -1;
  if (TP.chrg!=SIC_NOPIN) sic_gpio_mode_pullup(TP.chrg);
  if (TP.done!=SIC_NOPIN) sic_gpio_mode_pullup(TP.done);
  *out = &CHG;
  return 0;
}
static const sic_driver_t DRV = { .name="tp4057", .function=SIC_F_CHARGER, .probe=probe, .remove=NULL };
#ifdef __cplusplus
extern "C" { void sic_register_driver_tp4057(void){ sic_registry_register(&DRV); } }
#else
void sic_register_driver_tp4057(void){ sic_registry_register(&DRV); }
#endif
