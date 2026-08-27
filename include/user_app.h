#ifndef USER_APP_H_
#define USER_APP_H_

#include <stdint.h>

#include <arch_api.h>
#include <ke_msg.h>
#include <arch.h>

void app_on_init(void);

/* Recovery: invalidate the primary image, then reset -> failsafe. Runtime-read +
 * fail-safe (no-op if nothing validates). Shared by the 5-click gesture and the GATT
 * control point (cfw_ctrl.c, WITH_CTRL_POINT). */
void enter_failsafe(void);

#endif // USER_APP_H_
