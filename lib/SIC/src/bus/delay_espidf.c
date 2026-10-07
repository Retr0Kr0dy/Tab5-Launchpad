/*
 * delay_espidf.c — ESP-IDF native timing backend.
 *
 * Implements the timing half of the backend contract documented in
 * include/sic/sic_backend.h. Board-agnostic.
 */
#if defined(SIC_BACKEND_ESPIDF)

#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sic/bus/delay.h"

/*
 * esp_timer_get_time() returns microseconds since boot as int64_t. SIC's
 * contract is a 32-bit-ish millisecond counter (unsigned long), matching
 * Arduino millis(): it wraps, and callers already use subtraction-based
 * elapsed-time comparisons that are wrap-safe.
 */
unsigned long sic_millis(void)
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

void sic_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        taskYIELD();
        return;
    }
    /* pdMS_TO_TICKS() truncates; ensure a sub-tick delay still yields at
     * least one tick so callers never busy-return on a short delay. */
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
}

#endif /* SIC_BACKEND_ESPIDF */
