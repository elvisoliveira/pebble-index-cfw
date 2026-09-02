#ifndef SECRET_H_
#define SECRET_H_

/*
 * The shared secret between this ring and one phone.
 *
 * Trust on first use, gated by the button. The ring generates a 256-bit key and hands it
 * to a phone exactly when a human has just clicked — the connection has to be the one
 * that a click's burst brought in — and from then on the clip leaves the ring only under
 * that key, and the commands that can hurt (failsafe, ramp) carry a tag only that key can
 * make. Clicks stay public: the counter was always in the beacon, and five clicks need
 * a finger on the ring.
 *
 * What it costs to know: whoever hears the one notification that carries the key owns
 * it. Without an elliptic-curve exchange there is no closing that, and the button gate
 * shrinks it to a 3 s window the owner chose. A phone that connected in that window can
 * also RE-key a ring that already has a key. That is deliberate: it is how an owner with
 * a reinstalled app gets back in without dropping the ring into the failsafe. The cost
 * is that an attacker in the window can take the ring over until the owner's next
 * click, and the owner's app sees it at once — the key id in the beacon changes.
 *
 * The key lives in RAM. Every reboot — five clicks, the POR gesture, the NMI, a reflash —
 * forgets it, the beacon's key id drops to 0, and the app pairs again on the next click.
 * Persisting it would cost a flash sector whose address is not yet known to be free.
 *
 * Randomness comes from the SDK's ChaCha20 CSPRNG (CFG_USE_CHACHA20_RAND), which trng.c
 * seeds with 128 bits from the SRAM PUF at boot. Without that define the SDK falls back
 * to srand() with 32 bits, and a key drawn from it falls to brute force in seconds from
 * one captured tag — so the define is load-bearing, not a preference.
 */
#include <stdbool.h>
#include <stdint.h>
#include <chacha.h>

#define SECRET_TAG_LEN  16

/* A click happened: the connection this burst brings in may be issued a key. */
void secret_arm(void);

/* Connection state, from the SDK callbacks. Connecting consumes the arm into a per-link
 * right to be issued; disconnecting drops both, so a link from before the click, or one
 * after the phone's, gets nothing. */
void secret_on_connect(void);
void secret_on_disconnect(void);

/* Generate a fresh key and copy it out, if this link is allowed one. */
bool secret_issue(uint8_t out[CHACHA_KEY_LEN]);

/* The key, or NULL. clip_tx streams under it. */
const uint8_t *secret_key(void);

/* 16-bit id derived from the key, 0 when there is none. Goes in the beacon and the
 * transfer header so the phone can tell WHICH key, and that there is one, before it
 * connects. Reveals nothing about the key. */
uint16_t secret_id(void);

/* Fresh random nonce, one per transfer. */
void secret_nonce(uint8_t out[CHACHA_NONCE_LEN]);

/* Verify a command tag: the first SECRET_TAG_LEN bytes of the ChaCha20 block over the
 * command's nonce with the OPCODE as the counter — so a tag for one command cannot be
 * replayed as another. False when there is no key. Replaying the same command IS
 * possible; the only tagged commands are a reset and a test fill, and a reinstall
 * changes the key, so a replay buys nothing worth a counter. */
bool secret_check(uint8_t opcode, const uint8_t nonce[CHACHA_NONCE_LEN],
                  const uint8_t tag[SECRET_TAG_LEN]);

#endif // SECRET_H_
