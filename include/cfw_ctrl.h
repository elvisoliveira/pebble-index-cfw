#ifndef CFW_CTRL_H_
#define CFW_CTRL_H_

#include <stdint.h>

/*
 * GATT Control Point command dispatch. Two kinds of write:
 *
 *   plain
 *   [0x01, chunk_lo, chunk_hi]     send the recorded clip (clip_tx.c). Anyone may ask;
 *                                  the clip goes out encrypted under the ring's key,
 *                                  so asking buys ciphertext. The chunk size comes
 *                                  from the app because only it knows the MTU it
 *                                  negotiated. Refused while the ring has no key.
 *   [0x10]                         pair: issue the key. Answered on the Control Point
 *                                  with [0x10, key(32)] — once, to the connection a
 *                                  click's burst brought in (secret.h). Silently
 *                                  ignored otherwise, and when the Control Point is
 *                                  not subscribed, since the answer could not be heard.
 *
 *   tagged: [op, nonce(12), tag(16)], tag = ChaCha20(key, nonce, counter = op)[0..16]
 *   [0x00, ...]                    enter_failsafe(). Button-independent recovery, the
 *                                  same outcome as five fast clicks but reachable with
 *                                  a dead button — and, being a remote reset, the one
 *                                  command that must not be anyone's.
 *   [0x03, ...]                    fill the clip with a counting ramp. A test payload:
 *                                  corruption, a lost chunk or one out of order is
 *                                  obvious in a ramp and invisible in audio. Tagged
 *                                  because it overwrites a recording.
 *
 * Wired from ble_handlers.c (CUSTS1_VAL_WRITE_IND). The key model — what the tag
 * proves, what it does not, and why the key is handed out the way it is — is secret.h.
 */
void cfw_ctrl_write(uint8_t conidx, const uint8_t *data, uint16_t len);

#endif // CFW_CTRL_H_
