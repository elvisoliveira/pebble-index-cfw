/* ChaCha20 block function and stream XOR — see include/chacha.h. RFC 8439 section 2.3. */
#include <chacha.h>
#include <string.h>

static uint32_t rotl(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static uint32_t load32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void quarter_round(uint32_t *x, int a, int b, int c, int d)
{
    x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 16);
    x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 12);
    x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 8);
    x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 7);
}

void chacha20_block(const uint8_t key[CHACHA_KEY_LEN], const uint8_t nonce[CHACHA_NONCE_LEN],
                    uint32_t counter, uint8_t out[CHACHA_BLOCK_LEN])
{
    uint32_t s[16], x[16];

    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;  /* "expand 32-byte k" */
    for (int i = 0; i < 8; i++) {
        s[4 + i] = load32(key + 4 * i);
    }
    s[12] = counter;
    for (int i = 0; i < 3; i++) {
        s[13 + i] = load32(nonce + 4 * i);
    }
    memcpy(x, s, sizeof x);

    for (int i = 0; i < 10; i++) {          /* 20 rounds: 10 double rounds */
        quarter_round(x, 0, 4,  8, 12);
        quarter_round(x, 1, 5,  9, 13);
        quarter_round(x, 2, 6, 10, 14);
        quarter_round(x, 3, 7, 11, 15);
        quarter_round(x, 0, 5, 10, 15);
        quarter_round(x, 1, 6, 11, 12);
        quarter_round(x, 2, 7,  8, 13);
        quarter_round(x, 3, 4,  9, 14);
    }
    for (int i = 0; i < 16; i++) {
        store32(out + 4 * i, x[i] + s[i]);
    }
}

void chacha20_xor(const uint8_t key[CHACHA_KEY_LEN], const uint8_t nonce[CHACHA_NONCE_LEN],
                  uint32_t offset, uint8_t *buf, uint16_t len)
{
    uint8_t ks[CHACHA_BLOCK_LEN];
    uint8_t at = CHACHA_BLOCK_LEN;          /* forces a block on the first byte */

    while (len--) {
        if (at == CHACHA_BLOCK_LEN) {
            chacha20_block(key, nonce, offset / CHACHA_BLOCK_LEN, ks);
            at = offset % CHACHA_BLOCK_LEN;
        }
        *buf++ ^= ks[at++];
        offset++;
    }
}
