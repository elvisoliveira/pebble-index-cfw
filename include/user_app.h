#ifndef USER_APP_H_
#define USER_APP_H_

#include <stdint.h>

#include <arch_api.h>
#include <ke_msg.h>
#include <arch.h>

void app_on_init(void);

/* Recovery: invalidate every valid image bank in flash, then reset -> failsafe.
 * Runtime-read + fail-safe (no-op if nothing validates). Shared by the long-press
 * gesture and the GATT control point. */
void enter_failsafe(void);

/* GATT control-point command (redundant BLE path to recovery). Payload:
 *   [0x00]                          -> enter_failsafe() (self-locating, safe)
 *   [0x01][addr:4 LE][byte]         -> raw 1-byte flash write at addr, then reset
 * Covers the "gesture path failed but BLE is up" and "product-header offset wrong"
 * failure modes. See ble_handlers.c (CUSTS1_VAL_WRITE_IND). */
void cfw_ctrl_write(const uint8_t *data, uint16_t len);

#endif // USER_APP_H_
