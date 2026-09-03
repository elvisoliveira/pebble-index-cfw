#ifndef LED_H_
#define LED_H_

/*
 * RGB LED — the ring has one, and this plays the ring's own pattern format.
 *
 * Three channels, plain GPIO, driven HIGH to light and returned to INPUT (high-Z)
 * to go dark. No PWM: the factory firmware never assigns FUNC_PWMx to any pin, so
 * brightness is not controllable and the channels are on/off only. Pins per board
 * live in board_config.h.
 *
 * A pattern is a byte string, at most LED_PATTERN_MAX bytes, terminated the way the
 * ring terminates it — by a step whose duration is 0 (0x00 being the natural one):
 *
 *      7   6   5   4   3   2   1   0
 *    +---+---+---+---+---+---+---+---+
 *    | C | B | A |     duration      |   duration x 50 ms, 0 = stop
 *    +---+---+---+---+---+---+---+---+
 *      channels lit       0..31
 *
 * This is the ring's own encoding, so a pattern authored for the ring plays
 * identically here.
 */
#include <stdint.h>

/*
 * What each channel MEANS. Named by job rather than by colour, because the job is the
 * thing this file gets to decide and the colour is a board fact: which pad carries
 * which colour differs between the two boards, and on the ring it was unknown until a
 * real one answered it (2026-09-02 — the first guess had it recording in blue).
 * board_config.h is where job meets pin. Both boards now land on red / green / blue.
 */
#define LED_RECORD       (1u << 0)   /* channel A — recording in progress */
#define LED_TRANSFER     (1u << 1)   /* channel B — sending a clip to the phone */
#define LED_CLICK        (1u << 2)   /* channel C — a click was counted */

#define LED_CHANNELS     3
#define LED_PATTERN_MAX  25   /* the ring's own cap: it rejects 26 and above */
#define LED_UNIT_MS      50   /* one duration unit */

#define LED_STEP_DUR_MASK  0x1F   /* bits 4:0 — duration, in LED_UNIT_MS units */
#define LED_STEP_CH_SHIFT  5      /* bits 7:5 — channel mask */
#define LED_CH_MASK        0x07   /* one bit per channel, bit0 = A */

/* Build one step. mask is a 3-bit channel mask (bit0 = A), units are LED_UNIT_MS each.
 * Both fields are masked. For the channels this changes nothing the uint8_t cast did
 * not already do — excess bits die either way — it just states the containment where
 * the layout is defined instead of leaving it implicit in the cast. */
#define LED_STEP(mask, units) \
    ((uint8_t)((((mask) & LED_CH_MASK) << LED_STEP_CH_SHIFT) | ((units) & LED_STEP_DUR_MASK)))

_Static_assert(LED_STEP(7, 31) == 0xFF, "step: channels and duration must not overlap");
_Static_assert(LED_STEP(7, 31) >> LED_STEP_CH_SHIFT == 7, "channels must land in bits 7:5");
_Static_assert(LED_STEP(1 << LED_CHANNELS, 0) == 0, "an out-of-range channel must be dropped");

/* Start playing. Copies the pattern, so a caller's stack buffer is fine. A pattern
 * already playing is replaced. */
void led_play(const uint8_t *pattern, uint8_t len);

/* Stop and go dark. Safe to call when nothing is playing — and when nothing is lit it
 * skips the pad writes entirely. (A tidiness gate, not SWD protection: periph_init's
 * pad latch already claims the SWD pads for GPIO every boot — see board_config.h.) */
void led_off(void);

/* Cancel any pending advance without touching the pads or the pattern's light. For ISR
 * paths that must retire every stale timer handle before any allocation (on_wakeup):
 * led_off would also work there, but reads as removable darkness code — this cannot. */
void led_cancel(void);

/*
 * Light channels and leave them lit, until led_off() or the next led_play().
 *
 * Not expressible as a pattern, and that is the point: a pattern always ends, because
 * every step has a duration and the last one hands over to the terminator. Recording
 * lasts as long as a finger is on the button, which is not a duration this format can
 * hold — the longest single step is 31 units, 1.55 s. Reaching for led_play with a
 * maximum-length step LOOKS like a solid light and silently goes dark after those
 * 1.55 s.
 */
void led_hold(uint8_t channels);

/* Re-drive the lit channels after an extended-sleep wake. Call from periph_init (via
 * set_pad_functions), not from app_on_init: the GPIO configuration does not survive
 * extended sleep, and periph_init is what runs on every wake. */
void led_reapply(void);

#endif // LED_H_
