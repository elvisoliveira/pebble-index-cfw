#ifndef CLIP_TX_H_
#define CLIP_TX_H_

/*
 * Sending a recorded clip to the phone, over GATT notifications.
 *
 * Protocol, all of it:
 *
 *   advertisement    the clip's sample count is already in the beacon, and non-zero
 *                    IS "a clip is waiting" — no extra flag was needed
 *   app -> ring      enable notifications on the audio characteristic (REQUIRED: a
 *                    send with the CCCD clear is discarded and still reported as
 *                    delivered — see clip_tx_start)
 *   app -> ring      Control Point write {0x01, chunk_lo, chunk_hi}
 *   ring -> app      Control Point notify {0x01, samples_lo, samples_hi}   "starting"
 *   ring -> app      Audio notify, one chunk per SEND CONFIRMATION
 *   ring -> app      Control Point notify {0x02}                           "done"
 *
 * The chunk size comes from the app rather than being assumed here, because only the
 * app knows the MTU it negotiated. Guessing 244 and being wrong would truncate every
 * notification silently; guessing 20 would throw away twelve times the throughput.
 *
 * One chunk per confirmation is the whole flow control, and it is not optional: pushing
 * notifications in a loop exhausts the kernel message heap. CUSTS1_VAL_NTF_CFM is the
 * clock.
 *
 * No per-chunk header. The link delivers in order and retransmits, and the start and
 * done markers frame the transfer; offsets would buy detection of a firmware bug, not
 * of a radio one. Add them if a bug ever asks.
 */
#include <stdbool.h>
#include <stdint.h>

/* Begin sending. Ignored when there is nothing to send, a transfer is already up, or
 * the peer has not subscribed to the audio characteristic. */
void clip_tx_start(uint8_t conidx, uint16_t chunk);

/*
 * The peer wrote the audio CCCD — or the link dropped, which un-subscribes it.
 *
 * Tracked rather than read back because the SDK exposes no accessor for a CCC value, and
 * a write to one arrives here as an ordinary CUSTS1_VAL_WRITE_IND on the descriptor's
 * own handle (custs1_task.c:603-618). Wired in ble_handlers.c and user_on_disconnect.
 */
void clip_tx_set_subscribed(bool on);

/* A notification completed — send the next chunk. Wired to CUSTS1_VAL_NTF_CFM. */
void clip_tx_on_sent(uint16_t handle);

/*
 * Give up on a transfer that will never finish — the link dropped. Without it clip_tx
 * would stay "busy" forever: refusing every later recording, and holding its channel
 * lit with nothing left to turn it off.
 */
void clip_tx_abort(void);

/* True while a transfer is in flight. Recording refuses during one: the sender is
 * reading the very buffer a new recording would overwrite. */
bool clip_tx_busy(void);

#endif // CLIP_TX_H_
