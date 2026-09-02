#ifndef CLIP_TX_H_
#define CLIP_TX_H_

/*
 * Sending a recorded clip to the phone, over GATT notifications.
 *
 * Protocol, all of it:
 *
 *   advertisement    the clip's sample count is already in the beacon, and non-zero
 *                    IS "a clip is waiting" — no extra flag was needed
 *   app -> ring      enable notifications on BOTH characteristics — Audio and Control
 *                    Point (REQUIRED: a send with a CCCD clear is discarded and still
 *                    reported as delivered, and the framing markers travel on the
 *                    Control Point — see clip_tx_start)
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
 * the peer has not subscribed to both notifying characteristics. The chunk is clamped
 * to the connection's negotiated MTU as well as the characteristic length. */
void clip_tx_start(uint8_t conidx, uint16_t chunk);

/*
 * The peer wrote a CCCD — Audio or Control Point, identified by the descriptor handle.
 *
 * Tracked rather than read back because the SDK exposes no accessor for a CCC value, and
 * a write to one arrives here as an ordinary CUSTS1_VAL_WRITE_IND on the descriptor's
 * own handle (custs1_task.c:603-618). Wired in ble_handlers.c; a dropped link clears
 * both via clip_tx_abort. Re-checked mid-transfer: an unsubscribe stops the send with
 * the clip still held.
 */
void clip_tx_set_subscribed(uint16_t handle, bool on);

/* A notification completed — send the next chunk. Wired to CUSTS1_VAL_NTF_CFM. */
void clip_tx_on_sent(uint16_t handle);

/*
 * The link dropped: give up on a transfer that will never finish, and forget the
 * peer's subscriptions with it (a CCCD belongs to a connection, not to the ring).
 * Without it clip_tx would stay "busy" forever: refusing every later recording, and
 * holding its channel lit with nothing left to turn it off.
 */
void clip_tx_abort(void);

/* True while a transfer is in flight. Recording refuses during one: the sender is
 * reading the very buffer a new recording would overwrite. */
bool clip_tx_busy(void);

#endif // CLIP_TX_H_
