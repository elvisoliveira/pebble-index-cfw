#ifndef USER_APP_H_
#define USER_APP_H_

#include <stdint.h>

void app_on_init(void);

/* Recovery: invalidate the primary image, then reset -> failsafe. Runtime-read +
 * fail-safe (no-op if nothing validates). Shared by the 5-click gesture, the NMI
 * handler (interrupts.c) and the GATT control point (cfw_ctrl.c, WITH_CTRL_POINT). */
void enter_failsafe(void);

/* Burst-from-sleep advertising. Wired in user_callback_config.h.
 *
 * user_advertise_operation replaces the SDK's default_advertise_operation as
 * .default_operation_adv: it is the one point that sees every advertising start we do
 * not make ourselves, so it is where the "a burst is running" flag belongs. */
void user_advertise_operation(void);
void user_on_set_dev_config_complete(void);
void user_on_adv_undirect_complete(uint8_t status);

#endif // USER_APP_H_
