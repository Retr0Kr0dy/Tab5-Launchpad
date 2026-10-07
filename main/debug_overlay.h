/*
 * debug_overlay.h — small always-on-top perf readout (FPS, heap, panel
 * refresh ceiling), toggled from Settings. A debug tool, not a feature:
 * deliberately simple, green-on-black, fixed corner placement.
 */
#pragma once

#include "sgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* History, briefly (full account in CLAUDE.md's Rendering section --
 * this comment is the short version so the numbers below are at least
 * self-explanatory):
 *
 * Theoretical DSI scanout rate (lib/SGFX/src/drivers/st7121.c timing
 * config) computes to ~57.27Hz. Real measured present rate was first
 * found to be a uniform ~14.3 FPS across every mode. That uniformity
 * briefly looked like a clock-divider bug (57.27/4≈14.3), but the `perf`
 * console command (main/commands.c -- calls lp_draw_frame()+sgfx_present()
 * directly, unthrottled by this very pacing gate) measured real per-mode
 * draw/present costs and disproved that: a "minimal" control frame
 * (draw cost ≈0) measured present() alone at ~17.5ms ≈ 1/57.27Hz almost
 * exactly -- the panel genuinely scans out near its theoretical rate.
 * The ~14.3 FPS came from draw() cost (then ~35-53ms, dominated by
 * studio_ui.c's su_text() issuing one fill_rect() per antialiased RLE
 * run) plus up to one ~17.5ms vsync wait after it.
 *
 * su_text() was then rewritten to compose each glyph into a local buffer
 * and blit it via real set_window/write_pixels ops added to display.c,
 * instead of one fill_rect() per run. Measured effect (`perf`, same
 * methodology): draw cost barely moved (Notes 52.6ms -> 49.4ms) --
 * confirming the real bottleneck is CPU-to-PSRAM write BANDWIDTH for the
 * total pixel footprint touched, not fill_rect call count (a sibling
 * app's numbers, Tab5-Orion's Doom port, corroborate this: a single bulk
 * CPU blit of comparable pixel count costs ~37ms there too). But total
 * frame time still dropped substantially (Notes ~70ms -> ~53ms, Looper
 * ~52ms -> ~35ms) because present()'s vsync wait shrank -- draw() now
 * happens to land closer to a fresh vsync boundary. Real, measured,
 * reproducible; not yet the full fix. Getting draw cost itself down
 * further (toward Doom's PPA-accelerated 12.4ms and its resulting 31fps)
 * needs routing the composed buffer through the ESP32-P4's PPA instead of
 * a CPU write loop -- scoped in CLAUDE.md, not yet done.
 *
 * LP_PANEL_FRAME_MS below is the new measured worst-case total (Notes,
 * the heaviest mode, ~53ms, rounded up for jitter margin) -- same
 * "pacing shouldn't budget time that was never achievable" reasoning as
 * before, just with the post-blit numbers. */
#define LP_PANEL_REFRESH_HZ   18.2f
#define LP_PANEL_FRAME_MS     55   /* ~ceil(1000/18.2); see launchpad_app.c's frame pacing */

void debug_overlay_set_enabled(int on);
int  debug_overlay_enabled(void);

/* Call once per actually-presented frame (not per poll-loop tick) -- this
 * is what the FPS figure measures: real visual update rate, not how often
 * the loop happens to run. */
void debug_overlay_frame(void);

/* Draws the overlay box if enabled AND its own content actually changed
 * since the last draw (rate-limited internally so a fast loop doesn't
 * churn it needlessly). Returns 1 if it painted anything (caller should
 * then present), 0 otherwise -- same "tell the caller whether to bother
 * presenting" contract launchpad_grid.c's lp_draw_frame_delta() uses. */
int debug_overlay_draw(sgfx_device_t* d, int w, int h);

#ifdef __cplusplus
}
#endif
