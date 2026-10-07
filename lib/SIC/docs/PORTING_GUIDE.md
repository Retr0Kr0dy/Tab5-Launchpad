# Porting Guide (SIC v2)

1. **Create a board descriptor** in `lib/SIC/src/boards/board_<your>.c`.
2. **Wire buses only via HAL** (`sic_gpio_*`, `sic_i2c_*`, `sic_delay_*`).
3. **Add drivers** under `lib/SIC/src/drivers/<domain>/`.
4. **Register instances** in your board `probe` or a board-specific init.
5. Keep `src/main.cpp` using only `#include "sic/sic.h"`.
6. If a single I2C-expander chip (or similar shared resource) gates many independent subsystems'
   reset/enable lines, add one board-private shared module under `src/boards/<board>/` (see
   `src/boards/tab5/ioexpander.c` for a worked example) rather than duplicating expander I/O
   per-driver or inventing a registry-visible capability for it. Only promote it to a generic
   `src/drivers/` chip driver if a second board reuses the exact same chip.

See `lib/SIC/src/drivers/dummy.c` for a minimal reference driver.
