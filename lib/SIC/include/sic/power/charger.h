#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIC_CHG_NOT_PRESENT = 0,
    SIC_CHG_CHARGING,
    SIC_CHG_FULL,
    SIC_CHG_FAULT
} sic_chg_state_t;

int sic_charger_state(sic_chg_state_t* out);

struct charger_vtbl_s { int (*get_state)(const void*); };
typedef struct charger_s { const struct charger_vtbl_s* v; void* impl; } charger_t;
const charger_t* sic_charger(int index);

#ifdef __cplusplus
}
#endif
