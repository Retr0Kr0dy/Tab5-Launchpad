/*
 * wifi_power.c — Tab5-private power sequencing for the ESP32-C6 Wi-Fi/BT
 * co-processor.
 *
 * The ESP32-P4 has no radio of its own; Wi-Fi and Bluetooth come from an
 * onboard ESP32-C6 reached over SDIO (esp-hosted). Three signals gate whether
 * that co-processor is alive at all:
 *
 *   WLAN_PWR_EN   GPIO expander 1, bit 0   (its supply rail)
 *   antenna sel   GPIO expander 0, bit 0   (0 = internal, 1 = external)
 *   C6 reset      GPIO 15, a direct SoC pin (not an expander bit)
 *
 * The expander bits come from boards/tab5/ioexpander.c's documented
 * per-chip bit maps ("..., USB5V_EN(out,b3), NC(b2-1), WLAN_PWR_EN(out,b0)"
 * on expander 1; "..., SPK_EN(out,b1), RF ANT(out,b0)" on expander 0). GPIO
 * 15 is the C6's dedicated SDIO RST pin (CLK=12, CMD=13, D0=11, D1=10,
 * D2=9, D3=8, RST=15) — a real GPIO, so it is driven through SIC's own
 * sic_gpio_* HAL rather than the expander.
 *
 * ── Why this is a board-private module and not a driver ───────────────────
 * SIC never abstracts the Wi-Fi/BT protocol stack — esp-hosted's SDIO host
 * stack is consumed directly by app code. All SIC owns is this power/reset
 * sequence, which is board plumbing with no app-facing capability behind
 * it, the same reasoning that puts the GPIO expanders themselves in a
 * board-private module instead of the driver registry.
 *
 * ── What ioexpander.c's boot sequence already does, and what is left here ──
 * tab5_ioexp_init()'s boot sequence writes EXP1 OUT_SET = 0b00001001, whose
 * bit 0 is WLAN_PWR_EN — so the C6's rail is already enabled at boot, and
 * EXP0 OUT_SET = 0b01110110 leaves bit 0 clear, i.e. the internal antenna is
 * already selected. Neither of those needs changing here. What that init
 * doesn't touch is GPIO 15: the C6's dedicated reset line is not an
 * expander bit, is untouched by expander init, and comes out of chip reset
 * in an undefined input state. Releasing it deliberately, *after* the rail
 * is known good, is this function's real job.
 *
 * WLAN_PWR_EN is nonetheless re-asserted below rather than assumed. It costs
 * one I2C write, it makes the power-before-reset ordering explicit and
 * self-contained in one readable sequence, and it keeps the function correct
 * if it is ever called a second time after something else has cleared the bit
 * (e.g. a radio power-down path added later).
 *
 * Pure C99, platform isolation per docs/DESIGN_INVARIANTS.md: only sic_gpio_*,
 * sic_delay_ms and the (itself pure-C99) Tab5 ioexpander module. No
 * Arduino/ESP-IDF headers, so this file builds identically on both backends.
 */
#include <stdbool.h>

#include "sic/sic.h"
#include "sic/bus/gpio_bus.h"
#include "sic/bus/delay.h"
#include "boards/tab5/ioexpander.h"
#include "boards/tab5/wifi_power.h"

/* ESP32-C6 reset — a direct SoC GPIO, not an expander bit. */
#define TAB5_C6_RST_PIN        15

/* WLAN_PWR_EN: expander 1, bit 0. */
#define TAB5_WLAN_PWR_EXP      1
#define TAB5_WLAN_PWR_BIT      0

/* RF antenna select: expander 0, bit 0. Low = internal, high = external. */
#define TAB5_ANT_SEL_EXP       0
#define TAB5_ANT_SEL_BIT       0

/*
 * Timings. No reference source states required values for this board, so
 * these are deliberately generous round numbers rather than minimums: the
 * whole sequence runs once at boot, so tens of milliseconds cost nothing.
 *   SETTLE — rail stable (and reset comfortably held) before release.
 *   BOOT   — the C6 loading its SDIO slave firmware and becoming enumerable;
 *            the SDIO host bring-up that follows does its own retries, so
 *            this only has to avoid an obviously-too-early first attempt.
 */
#define TAB5_C6_PWR_SETTLE_MS  20
#define TAB5_C6_BOOT_MS        50

void tab5_wifi_power_init(void)
{
    /*
     * Ordering is the entire point of this function: the C6 must be held in
     * reset across the moment its rail comes up, and reset must be released
     * only once that rail is stable. Releasing reset into an unpowered or
     * still-ramping supply is exactly the failure this sequence prevents.
     */

    /* 1. Assert reset first, before anything touches the power rail. Driving
     *    the pin as an output is itself the assert — until now it is an
     *    undriven input. */
    sic_gpio_mode(TAB5_C6_RST_PIN, 1);
    sic_gpio_write(TAB5_C6_RST_PIN, 0);

    /* 2. Rail on. Already high from tab5_ioexp_init()'s boot OUT_SET in the
     *    normal path; re-asserted so the ordering above holds unconditionally
     *    and so a later call after a power-down still works. */
    tab5_ioexp_set(TAB5_WLAN_PWR_EXP, TAB5_WLAN_PWR_BIT, 1);

    /* 3. Let the supply settle while reset is still held low. */
    sic_delay_ms(TAB5_C6_PWR_SETTLE_MS);

    /* 4. Power is now known good — release reset and let the C6 boot. */
    sic_gpio_write(TAB5_C6_RST_PIN, 1);
    sic_delay_ms(TAB5_C6_BOOT_MS);

    /* Antenna select is left at the state tab5_ioexp_init() established
     * (EXP0 bit 0 clear = internal). Callers wanting the external antenna
     * call tab5_wifi_set_ext_antenna(true); re-driving it here would just be
     * an I2C write that changes nothing. */
}

void tab5_wifi_set_ext_antenna(bool ext)
{
    tab5_ioexp_set(TAB5_ANT_SEL_EXP, TAB5_ANT_SEL_BIT, ext ? 1 : 0);
}
