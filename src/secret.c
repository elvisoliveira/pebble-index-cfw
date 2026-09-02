/* The ring's shared secret — see include/secret.h. */
#include <secret.h>
#include <chacha20.h>       /* the SDK's CSPRNG: csprng_get_next_uint32 */
#include <string.h>

static uint8_t  key[CHACHA_KEY_LEN];
static uint16_t id;             /* 0 = no key */
static bool     armed;          /* a click, not yet consumed by a connection */
static bool     issuable;       /* this link came in on a click: may be issued a key */

static void fill_random(uint8_t *out, uint8_t len)
{
    while (len) {
        uint32_t r = csprng_get_next_uint32();
        uint8_t n = len < 4 ? len : 4;
        memcpy(out, &r, n);
        out += n;
        len -= n;
    }
}

void secret_arm(void)
{
    armed = true;
}

void secret_on_connect(void)
{
    issuable = armed;
    armed = false;
}

void secret_on_disconnect(void)
{
    issuable = false;
    armed = false;
}

bool secret_issue(uint8_t out[CHACHA_KEY_LEN])
{
    if (!issuable) {
        return false;
    }
    fill_random(key, sizeof key);

    /* The id is the first two bytes of the block over a zero nonce and counter 0.
     * Counter 0 is also the failsafe opcode, so a failsafe tag sent over an all-zero
     * nonce would share this block; the id exposes two of its sixteen bytes, which
     * leaves fourteen to guess. The clip never shares it: its nonce is random. */
    uint8_t block[CHACHA_BLOCK_LEN];
    static const uint8_t zero_nonce[CHACHA_NONCE_LEN];
    chacha20_block(key, zero_nonce, 0, block);
    id = (uint16_t)(block[0] | (block[1] << 8));
    if (id == 0) {
        id = 1;                     /* 0 is reserved for "no key" */
    }
    memcpy(out, key, sizeof key);
    return true;
}

const uint8_t *secret_key(void)
{
    return id ? key : NULL;
}

uint16_t secret_id(void)
{
    return id;
}

void secret_nonce(uint8_t out[CHACHA_NONCE_LEN])
{
    fill_random(out, CHACHA_NONCE_LEN);
}

bool secret_check(uint8_t opcode, const uint8_t nonce[CHACHA_NONCE_LEN],
                  const uint8_t tag[SECRET_TAG_LEN])
{
    if (!id) {
        return false;
    }
    uint8_t block[CHACHA_BLOCK_LEN];
    chacha20_block(key, nonce, opcode, block);
    /* Constant-time compare. Timing over BLE is far too coarse to exploit a byte-wise
     * memcmp, but this costs nothing and removes the question. */
    uint8_t diff = 0;
    for (int i = 0; i < SECRET_TAG_LEN; i++) {
        diff |= block[i] ^ tag[i];
    }
    return diff == 0;
}
