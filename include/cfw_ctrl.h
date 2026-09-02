#ifndef CFW_CTRL_H_
#define CFW_CTRL_H_

#include <stdint.h>

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
 * Wired from ble_handlers.c (CUSTS1_VAL_WRITE_IND).
 *
 * NO AUTHENTICATION. BLE security is compiled out (da1458x_config_basic.h) and the
 * advertiser accepts any central, so every opcode here is available to anyone in radio
 * range: 0x00 is a remote reset into recovery, 0x01 hands out the last recording. Both
 * were fine for a click counter and are a known, documented gap now that the ring holds
 * audio (README, "Security"). Bonding or an unlock token would close it; neither is
 * built, and anything added here inherits the gap until one is.
 */
void cfw_ctrl_write(uint8_t conidx, const uint8_t *data, uint16_t len);

#endif // CFW_CTRL_H_
