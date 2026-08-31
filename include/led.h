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
 * Read out of the stock app v3.74: FUN_07fc4e1c (loader), FUN_07fc4eb8 (step
 * advance), FUN_07fc4f20 (pin driver). Same encoding, so a pattern authored for the
 * ring plays identically here.
 */
#include <stdint.h>

#define LED_CHANNELS     3
#define LED_PATTERN_MAX  25   /* the ring's own cap (FUN_07fc4e1c rejects >= 26) */

/* Build one step. mask is a 3-bit channel mask (bit0 = A), units are 50 ms each. */
#define LED_STEP(mask, units)  ((uint8_t)(((mask) << 5) | ((units) & 0x1F)))

/* Start playing. Copies the pattern, so a caller's stack buffer is fine. A pattern
 * already playing is replaced. */
void led_play(const uint8_t *pattern, uint8_t len);

/* Stop and go dark. Safe to call when nothing is playing. */
void led_off(void);

#endif // LED_H_
