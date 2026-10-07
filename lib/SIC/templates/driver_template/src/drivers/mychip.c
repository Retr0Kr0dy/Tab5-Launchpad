#include "sic/driver/mychip.h"
#include "sic/log.h"
int sic_mychip_begin(void){ sic_log_info("mychip: begin"); return 0; }
int sic_mychip_read(int *value){ if(!value) return -1; *value=42; return 0; }
