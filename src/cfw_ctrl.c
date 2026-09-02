#include <cfw_ctrl.h>
#include <user_app.h>   /* enter_failsafe() — shared with the 5-click gesture */
#include <clip_tx.h>
#include <mic.h>
#include <secret.h>

#define OP_FAILSAFE  0x00
#define OP_SEND      0x01
#define OP_RAMP      0x03
#define OP_PAIR      0x10

#define TAGGED_LEN   (1 + CHACHA_NONCE_LEN + SECRET_TAG_LEN)

/* Opcodes are documented in cfw_ctrl.h. The old raw-flash escape hatch (write to an
 * address) is gone; the flasher app covers recovery over BLE. */
static bool tagged(const uint8_t *data, uint16_t len)
{
    return len >= TAGGED_LEN && secret_check(data[0], &data[1], &data[1 + CHACHA_NONCE_LEN]);
}

void cfw_ctrl_write(uint8_t conidx, const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return;
    }
    switch (data[0]) {
    case OP_FAILSAFE:
        if (tagged(data, len)) {
            enter_failsafe();
        }
        break;
    case OP_SEND:
        /* Two bytes of chunk size, little-endian; absent means "be safe", which is the
         * 20 bytes a default MTU carries. clip_tx clamps either way. */
        clip_tx_start(conidx, (len >= 3) ? (uint16_t)(data[1] | (data[2] << 8)) : 0);
        break;
    case OP_RAMP:
        /* Same buffer clip_tx streams from: filling it mid-transfer would rewrite the
         * bytes in flight AND reset the sample count the transfer's own total was
         * computed from. The recording path refuses for the same reason; there it is
         * mic_capture's blocking that makes the check unreachable, here nothing does. */
        if (tagged(data, len) && !clip_tx_busy()) {
            mic_fill_ramp();
        }
        break;
    case OP_PAIR: {
        /* Subscription first, key second: custs1 skips-but-confirms a notification to
         * a clear CCCD (clip_tx.c), so issuing before checking would burn the ring's
         * one chance per click on an answer nobody hears, and the phone would be
         * locked out until the next click. */
        uint8_t msg[1 + CHACHA_KEY_LEN] = { OP_PAIR };
        if (clip_tx_ctrl_subscribed() && secret_issue(&msg[1])) {
            clip_tx_ctrl_notify(conidx, msg, sizeof msg);
            user_beacon_restage();      /* the beacon carries the new key id */
        }
        break;
    }
    default:
        break;
    }
}
