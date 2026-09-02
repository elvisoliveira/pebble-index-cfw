/*
 * Host-side check of src/chacha.c against RFC 8439's own vectors — the one runnable
 * check the cipher leaves behind, since nothing on the ring can tell a wrong keystream
 * from a right one. The phone can: a mismatch here is a clip that decrypts to noise.
 *
 *   cc -Iinclude tests/chacha_test.c src/chacha.c -o /tmp/chacha_test && /tmp/chacha_test
 */
#include <chacha.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;

    /* 2.3.2: block function, counter 1 */
    static const uint8_t nonce_a[12] = {0,0,0,9, 0,0,0,0x4a, 0,0,0,0};
    static const uint8_t want_a[64] = {
        0x10,0xf1,0xe7,0xe4,0xd1,0x3b,0x59,0x15,0x50,0x0f,0xdd,0x1f,0xa3,0x20,0x71,0xc4,
        0xc7,0xd1,0xf4,0xc7,0x33,0xc0,0x68,0x03,0x04,0x22,0xaa,0x9a,0xc3,0xd4,0x6c,0x4e,
        0xd2,0x82,0x64,0x46,0x07,0x9f,0xaa,0x09,0x14,0xc2,0xd7,0x05,0xd9,0x8b,0x02,0xa2,
        0xb5,0x12,0x9c,0xd1,0xde,0x16,0x4e,0xb9,0xcb,0xd0,0x83,0xe8,0xa2,0x50,0x3c,0x4e,
    };
    uint8_t out[64];
    chacha20_block(key, nonce_a, 1, out);
    assert(memcmp(out, want_a, 64) == 0);

    /* 2.4.2: encryption, counter 1 == stream offset 64, and a chunk boundary mid-block
     * must not disturb the stream — the clip goes out in chunks. */
    static const uint8_t nonce_b[12] = {0,0,0,0, 0,0,0,0x4a, 0,0,0,0};
    const char *plain = "Ladies and Gentlemen of the class of '99: If I could offer you "
                        "only one tip for the future, sunscreen would be it.";
    static const uint8_t want_b[16] = {
        0x6e,0x2e,0x35,0x9a,0x25,0x68,0xf9,0x80,0x41,0xba,0x07,0x28,0xdd,0x0d,0x69,0x81,
    };
    uint8_t buf[114];
    memcpy(buf, plain, sizeof buf);
    chacha20_xor(key, nonce_b, 64, buf, 50);                 /* first chunk  */
    chacha20_xor(key, nonce_b, 64 + 50, buf + 50, 64);       /* second chunk */
    assert(memcmp(buf, want_b, 16) == 0);
    chacha20_xor(key, nonce_b, 64, buf, 114);                /* decrypt in one go */
    assert(memcmp(buf, plain, 114) == 0);

    puts("chacha: ok");
    return 0;
}
