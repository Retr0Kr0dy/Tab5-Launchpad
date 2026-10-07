/*
 * test_tab5_link.c — native link-check harness for the Tab5 board descriptor.
 *
 * Not a PlatformIO/CI-wired test (no ESP-IDF/Arduino toolchain is available
 * in this environment); a standalone `gcc -std=c99` build that links every
 * pure-C99 file autoreg.c now references for SIC_TARGET_TAB5 against
 * tests/native/posix_stub_backend.c's stand-in backend, then actually calls
 * sic_begin_legacy()/sic_begin() so the whole board-descriptor -> autoreg ->
 * probe chain executes, not just compiles. See build_and_run.sh in this
 * directory for the exact command line.
 *
 * Deliberately excluded (would need real ESP-IDF headers, not available
 * here): src/drivers/storage/sd_sdmmc_espidf.c and
 * src/backends/espidf/sic_espidf_camera.c. SIC_BACKEND_ESPIDF is left
 * undefined for this build, so autoreg.c's own guards (see src/core/
 * autoreg.c's SIC__AUTOREG_SD_SDMMC / SIC__AUTOREG_TAB5_CAM_ESPIDF, added
 * this phase specifically so a non-ESP-IDF SIC_TARGET_TAB5 build like this
 * one does not reference an undefined registration symbol) correctly leave
 * those two out rather than failing to link.
 */
#include <stdio.h>
#include "sic/sic.h"
#include "sic/audio/mic.h"
#include "sic/audio/amp.h"

int main(void) {
    printf("[TEST] Tab5 native link check\n");

    const sic_board_t* b = sic_board_default();
    if (!b) { printf("[FAIL] sic_board_default() returned NULL\n"); return 1; }
    printf("[OK] sic_board_default() -> board \"%s\", %d ICs\n", b->name, b->ic_count);

    sic_begin_opts_t opts = { .init_buses = 0, .lazy_drivers = 0 };
    int rc = sic_begin(b, &opts);
    printf("[OK] sic_begin rc=%d\n", rc);

    printf("[INFO] touch=%d rtc=%d imu=%d charger=%d sd=%d camera=%d mic=%d amp=%d\n",
           sic_count_cap(SIC_CAP_TOUCH), sic_count_cap(SIC_CAP_RTC),
           sic_count_cap(SIC_CAP_IMU), sic_count_cap(SIC_CAP_CHARGER),
           sic_count_cap(SIC_CAP_SD), sic_count_cap(SIC_CAP_CAMERA),
           sic_count_cap(SIC_CAP_MIC), sic_count_cap(SIC_CAP_AUDIO_AMP));

    /* Touch: expect exactly 1 in this stub, since sic_i2c_scan_bus() always
     * reports "nothing present" (see posix_stub_backend.c) -- both st712x
     * hints reject (tab5_panel_detected() falls back to
     * TAB5_PANEL_ILI9881C_GT911, panel_detect.c's own final-fallback path
     * for "neither 0x14 nor 0x55 ACKed"), leaving only touch_gt911's
     * unconditional probe to succeed. This exercises the exact fallback
     * path panel_detect.c documents for real hardware where detection
     * itself fails, and confirms the "GT911 listed first" LIFO ordering
     * reasoning in board_tab5.c doesn't accidentally depend on any other
     * entry being present. */
    int touch_n = sic_count_cap(SIC_CAP_TOUCH);
    if (touch_n != 1) {
        printf("[FAIL] expected exactly 1 touch instance in the stub-I2C fallback path, got %d\n", touch_n);
        return 1;
    }

    /* RTC/IMU/SD/CAMERA/MIC/AMP: probe() never touches I2C (DESIGN_INVARIANTS.md),
     * so all of these register regardless of the stub's "nothing on the bus"
     * behavior -- only their first real start()/read()/enable() call would see
     * the stub's I2C failures. */
    if (sic_count_cap(SIC_CAP_RTC) != 1)   { printf("[FAIL] rtc\n");   return 1; }
    if (sic_count_cap(SIC_CAP_IMU) != 1)   { printf("[FAIL] imu\n");   return 1; }
    if (sic_count_cap(SIC_CAP_CHARGER) != 1){ printf("[FAIL] charger\n"); return 1; }
    if (sic_count_cap(SIC_CAP_MIC) != 1)   { printf("[FAIL] mic\n");   return 1; }
    if (sic_count_cap(SIC_CAP_AUDIO_AMP) != 1) { printf("[FAIL] amp\n"); return 1; }
    /* SD/camera excluded from this build (ESP-IDF-only, see file header). */
    if (sic_count_cap(SIC_CAP_SD) != 0)    { printf("[FAIL] sd (expected 0, ESP-IDF-only driver excluded)\n"); return 1; }
    if (sic_count_cap(SIC_CAP_CAMERA) != 0){ printf("[FAIL] camera (expected 0, ESP-IDF-only driver excluded)\n"); return 1; }

    /* Exercise each vtable's lazily-deferred I2C/I2S path once, to prove the
     * whole chain (SIC HAL -> stub backend) actually executes, not just links. */
    sic_rtc_time_t t;
    (void)sic_rtc(0)->v->get_time(sic_rtc(0), &t);
    sic_imu_sample_t s;
    (void)sic_imu(0)->v->read(sic_imu(0), &s);
    sic_chg_state_t cs;
    (void)sic_charger_state(&cs);
    sic_touch_point_t pts[4];
    (void)sic_touch(0)->v->read_points(sic_touch(0), pts, 4);
    (void)sic_mic(0)->v->start(sic_mic(0), 16000);
    sic_amp(0)->v->enable(sic_amp(0), 1);

    printf("[OK] Tab5 native link check passed\n");
    return 0;
}
