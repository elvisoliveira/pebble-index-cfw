/* user_periph_setup.h — peripherals setup (from the Dialog SDK template). Several SDK
 * .c files include this header, so the includes below stay. */

#ifndef _USER_PERIPH_SETUP_H_
#define _USER_PERIPH_SETUP_H_

#include "rwip_config.h"
#include "da1458x_periph_setup.h"
#include "gpio.h"

void periph_init(void);

/*
 * Last-resort reset: hold the button and the SILICON resets the chip, with no code
 * involved — POR_PIN_REG selects the pad, POR_TIMER_REG the hold. It is the only
 * recovery this firmware has that survives its own firmware being wedged, and on a
 * sealed ring with no removable battery it is the equivalent of a power cycle.
 *
 * It is armed from set_pad_functions(), which periph_init runs on EVERY wake, not from
 * app_on_init which runs once. That is what makes por_disarm() safe: a path that forgets
 * to re-arm loses the net only until the next sleep, instead of silently forever.
 */
void por_arm(void);

/*
 * The POR hold, in units of 4096 RC32 periods (gpio.h: time = por_time x 4096 x RC32
 * period), a 7-bit field, so ~16 s is the hardware ceiling. 40 is nominally ~5 s.
 *
 * NOMINALLY is the word, and the macro name says so: RC32 is an uncalibrated RC
 * oscillator, not a crystal. The SDK does not state its frequency, 4096 periods is
 * ~128 ms at a nominal 32 kHz, and the real figure moves with temperature and supply.
 * Treat the window as a range of several seconds around 5, never as a deadline.
 *
 * Public because it is half of a relation whose other half lives in user_app.c: the
 * hold that starts a recording must expire well before this, or every hold reboots the
 * ring instead of recording. That was prose in two files and enforced nowhere; the
 * assert there is sized for the spread above, which is why it asks for 2x and not a
 * few percent.
 */
#define POR_HOLD_TICKS      40
#define POR_HOLD_MS_NOMINAL (POR_HOLD_TICKS * 128)

/*
 * Disarm it. Called when the firmware has PROVEN it saw a button hold — which is the
 * whole trick: a hold means "record" to healthy firmware and "reset" to wedged
 * firmware, and the disarm is what distinguishes them. Nothing else is needed to tell
 * the two apart, and the user has one gesture to learn instead of two.
 *
 * Disarm on GESTURE DETECTED, never on "recording actually started": a legitimate
 * refusal to record (buffer full, capture already running, ADC unavailable) must not
 * reboot the device. What is being tested is whether the firmware responds at all.
 */
void por_disarm(void);

#endif // _USER_PERIPH_SETUP_H_
