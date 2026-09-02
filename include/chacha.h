#ifndef CHACHA_H_
#define CHACHA_H_

/*
 * ChaCha20, RFC 8439 layout: 256-bit key, 96-bit nonce, 32-bit block counter.
 *
 * The one primitive the CFW's security rests on, used two ways: as a stream cipher over
 * the clip (chacha20_xor) and as a keyed function for command tags (one block, the
 * opcode as the counter — see secret.c). Written here rather than taken from the SDK
 * because the SDK's copy is a random-number generator with its own state layout and a
 * 128-bit key, exported only as csprng_seed / csprng_get_next_uint32; it cannot be
 * keyed by us without clobbering the stack's RNG, and it does not match what the phone
 * has. This layout is what Android ships as Cipher "ChaCha20" (API 28+), so the app
 * side needs no crypto of its own.
 *
 * No AES. The software AES in the SDK is ~3.5 KB of code and tables; the hardware path
 * goes through the BLE controller, asynchronously, by kernel message. Forty lines with
 * no tables won.
 *
 * No SDK includes, on purpose: tests/chacha_test.c compiles this on the host against
 * the RFC's own vectors.
 */
#include <stdint.h>

#define CHACHA_KEY_LEN    32
#define CHACHA_NONCE_LEN  12
#define CHACHA_BLOCK_LEN  64

/* One keystream block. */
void chacha20_block(const uint8_t key[CHACHA_KEY_LEN], const uint8_t nonce[CHACHA_NONCE_LEN],
                    uint32_t counter, uint8_t out[CHACHA_BLOCK_LEN]);

/* XOR buf with the keystream, starting at byte `offset` of the stream (block 0 begins at
 * offset 0). Encrypt and decrypt are the same call; a transfer that sends the clip in
 * chunks passes each chunk's byte offset and needs no state between them. */
void chacha20_xor(const uint8_t key[CHACHA_KEY_LEN], const uint8_t nonce[CHACHA_NONCE_LEN],
                  uint32_t offset, uint8_t *buf, uint16_t len);

#endif // CHACHA_H_
