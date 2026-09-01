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
