#include <cfw_ctrl.h>

#if defined(WITH_CTRL_POINT)
#include <user_app.h>   /* enter_failsafe() — shared with the 5-click gesture */
#include <clip_tx.h>
#include <mic.h>

/* Opcodes are documented in cfw_ctrl.h. The old raw-flash escape hatch is gone;
 * recovery-by-address remains in bench/cfw-enter-failsafe.py. */
void cfw_ctrl_write(uint8_t conidx, const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return;
    }
    switch (data[0]) {
    case 0x00:
        enter_failsafe();
        break;
    case 0x01:
        /* Two bytes of chunk size, little-endian; absent means "be safe", which is the
         * 20 bytes a default MTU carries. clip_tx clamps either way. */
        clip_tx_start(conidx, (len >= 3) ? (uint16_t)(data[1] | (data[2] << 8)) : 0);
        break;
    case 0x03:
        /* Same buffer clip_tx streams from: filling it mid-transfer would rewrite the
         * bytes in flight AND reset the sample count the transfer's own total was
         * computed from. The recording path refuses for the same reason; there it is
         * mic_capture's blocking that makes the check unreachable, here nothing does. */
        if (!clip_tx_busy()) {
            mic_fill_ramp();
        }
        break;
    default:
        break;
    }
}
#endif
