#ifndef CLIP_TX_H_
#define CLIP_TX_H_

/*
 * Sending a recorded clip to the phone, over GATT notifications.
 *
 * Protocol, all of it:
 *
 *   advertisement    the clip's sample count is already in the beacon, and non-zero
 *                    IS "a clip is waiting" — no extra flag was needed. Beside it, the
 *                    key id (secret.h): 0 means the ring has no key and must be paired
 *                    before a fetch can succeed
 *   app -> ring      enable notifications on BOTH characteristics — Audio and Control
 *                    Point (REQUIRED: a send with a CCCD clear is discarded and still
 *                    reported as delivered, and the framing markers travel on the
 *                    Control Point — see clip_tx_start)
 *   app -> ring      Control Point write {0x01, chunk_lo, chunk_hi}
 *   ring -> app      Control Point notify {0x01, samples(2), key_id(2), nonce(12)}
 *                    "starting" — the key id says which key, the nonce is this
 *                    transfer's, fresh every time
 *   ring -> app      Audio notify, one chunk per SEND CONFIRMATION, each chunk XOR
 *                    ChaCha20(key, nonce) at the chunk's byte offset in the clip
 *   ring -> app      Control Point notify {0x02}                           "done"
 *
 * Encrypted, not authenticated: a stream cipher with no MAC. Flipping bits needs a
 * man in the middle on a BLE link, and what it would produce is noise. Not worth 16
 * bytes a chunk.
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

/* Begin sending. Ignored when there is nothing to send, a transfer is already up, the
 * ring has no key to send under, or the peer has not subscribed to both notifying
 * characteristics. The chunk is clamped to the connection's negotiated MTU as well as
 * the characteristic length. */
void clip_tx_start(uint8_t conidx, uint16_t chunk);

/* The Control Point's notification plumbing, for cfw_ctrl's pairing answer: is the peer
 * subscribed, and send it something. Kept here because this file already owns the
 * CUSTS1 message and the CCCD bookkeeping. */
bool clip_tx_ctrl_subscribed(void);
void clip_tx_ctrl_notify(uint8_t conidx, const uint8_t *data, uint16_t len);

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
