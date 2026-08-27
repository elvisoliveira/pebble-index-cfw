#include <cfw_ctrl.h>

#if defined(WITH_CTRL_POINT)
#include <user_app.h>   /* enter_failsafe() — shared with the 5-click gesture */

/*
 * The featureless CFW's only control-point command: write 0x00 to drop into the failsafe
 * bootloader. enter_failsafe() itself lives in user_app.c (shared) because the 5-click
 * gesture calls it too — this file just routes the GATT command to it. The old 0x01 raw
 * flash-write escape hatch was dropped for minimalism; recovery-by-address stays in the
 * bench tooling (bench/cfw-enter-failsafe.py and friends).
 */
void cfw_ctrl_write(const uint8_t *data, uint16_t len)
{
    if (len < 1) return;
    if (data[0] == 0x00) enter_failsafe();
}
#endif
