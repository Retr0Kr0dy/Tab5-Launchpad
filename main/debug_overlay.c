#include "debug_overlay.h"
#include "orion.h"
#include "studio_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

static int      s_enabled;
static uint32_t s_frame_count;
static uint32_t s_window_start_ms;
static float    s_fps;
static uint32_t s_last_update_ms;
static char     s_last_text[64] = "";

void debug_overlay_set_enabled(int on)
{
    s_enabled = on ? 1 : 0;
    if (s_enabled) {
        s_frame_count = 0;
        s_window_start_ms = orion_millis();
        s_last_text[0] = '\0';
    }
}

int debug_overlay_enabled(void)
{
    return s_enabled;
}

void debug_overlay_frame(void)
{
    s_frame_count++;
}

int debug_overlay_draw(sgfx_device_t* d, int w, int h)
{
    if (!s_enabled) {
        return 0;
    }

    uint32_t now = orion_millis();

    /* FPS window: recompute every 500ms from the frames counted since the
     * last window, then reset the counter -- a simple, cheap rolling
     * measure, accurate enough for a debug readout. */
    if (now - s_window_start_ms >= 500) {
        float secs = (float)(now - s_window_start_ms) / 1000.0f;
        s_fps = (secs > 0.0f) ? (float)s_frame_count / secs : 0.0f;
        s_frame_count = 0;
        s_window_start_ms = now;
    }

    if (now - s_last_update_ms >= 150 || !s_last_text[0]) {
        s_last_update_ms = now;
        size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        snprintf(s_last_text, sizeof s_last_text, "%.1f fps / %u KB", (double)s_fps, (unsigned)(free_bytes / 1024));
    }
    (void)h;
    su_rect box = {w / 2 - 130, 8, 260, 36};
    su_fill(d, box, SU_BG);
    su_center(d, box, s_last_text, 18, SU_DIM, SU_BG);
    return 1;
}
