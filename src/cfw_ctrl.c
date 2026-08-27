#include <cfw_ctrl.h>

#if defined(WITH_CTRL_POINT)
#include <user_app.h>   /* enter_failsafe() — shared with the 5-click gesture */

/* The only command is 0x00 -> failsafe. The old 0x01 raw-flash escape hatch was
 * dropped; recovery-by-address remains in bench/cfw-enter-failsafe.py. */
void cfw_ctrl_write(const uint8_t *data, uint16_t len)
{
    if (len && data[0] == 0x00)
        enter_failsafe();
}
#endif
