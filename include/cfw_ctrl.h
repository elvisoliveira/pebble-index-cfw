#ifndef CFW_CTRL_H_
#define CFW_CTRL_H_

#include <stdint.h>

#if defined(WITH_CTRL_POINT)
/*
 * GATT Control Point command dispatch. Payload:
 *   [0x00]                        -> enter_failsafe(). Button-independent recovery, the
 *                                    same outcome as five fast clicks but reachable with
 *                                    a dead button.
 *   [0x01, chunk_lo, chunk_hi]    -> send the recorded clip. The chunk size comes from
 *                                    the app because only it knows the MTU it negotiated.
 *   [0x03]                        -> fill the clip with a counting ramp. A test payload:
 *                                    corruption, a lost chunk or one out of order is
 *                                    obvious in a ramp and invisible in audio, so the
 *                                    transfer can be proven before real audio exists.
 * Wired from ble_handlers.c (CUSTS1_VAL_WRITE_IND). Compiled only when WITH_CTRL_POINT
 * is defined (da1458x_config_basic.h); off => the whole CUSTS1 profile drops.
 */
void cfw_ctrl_write(uint8_t conidx, const uint8_t *data, uint16_t len);
#endif

#endif // CFW_CTRL_H_
