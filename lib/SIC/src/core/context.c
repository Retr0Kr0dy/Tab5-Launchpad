// tiny context and no-op log fallback
#include "sic/sic.h"

#ifndef SIC_LOG
#define SIC_LOG(...) do{}while(0)
#endif
void sic__context_touch(void){}
