#ifndef CFW_CTRL_H_
#define CFW_CTRL_H_

#include <stdint.h>
#include <da1458x_config_basic.h>   /* WITH_CTRL_POINT */

#if defined(WITH_CTRL_POINT)
/*
 * GATT Control Point command dispatch — a redundant, button-independent path to
 * recovery. Payload:
 *   [0x00]  -> enter_failsafe()   (same as 5 fast clicks, but works even with a dead button)
 * Wired from ble_handlers.c (CUSTS1_VAL_WRITE_IND). Compiled only when WITH_CTRL_POINT
 * is defined (da1458x_config_basic.h); off => the whole CUSTS1 profile drops.
 */
void cfw_ctrl_write(const uint8_t *data, uint16_t len);
#endif

#endif // CFW_CTRL_H_
