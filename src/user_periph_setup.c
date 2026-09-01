/* Peripherals setup and initialization (from the Dialog SDK template). */
#include <user_periph_setup.h>
#include <datasheet.h>
#include <system_library.h>
#include <rwip_config.h>
#include <syscntl.h>
#include <gpio.h>
#include <arch_wdg.h>
#include <board_config.h>   /* BTN_PORT/BTN_PIN — ring vs kit (TARGET_KIT) */
#include <led.h>
#include <board_config.h>

static void set_pad_functions(void)
{
    /* Active-low button pull-up (BTN_PIN from board_config.h).
     * Without it the line doesn't return to HIGH on release, the rising edge vanishes,
     * and the wkupct one-shot re-arm dies after the 1st click. It MUST live here, not in
     * app_on_init: periph_init re-runs on every wake from extended sleep; app_on_init runs
     * only once. */
    GPIO_ConfigurePin(BTN_PORT, BTN_PIN, INPUT_PULLUP, PID_GPIO, false);

    /* Same reason, same place: extended sleep loses the GPIO configuration, so a lit
     * LED channel has to be re-driven here or it goes dark at the first sleep inside a
     * pattern. No-op while the LED is dark. (No SWD implications either way: the pad
     * latch call below hands P0_2/P0_10 to GPIO regardless — see board_config.h.) */
    led_reapply();

    /* Re-armed on every wake, deliberately — see por_arm()'s comment. */
    por_arm();
}

/* por_time is in units of 4096 RC32K periods, about 128 ms, in a 7-bit field: ~16 s is
 * the hardware ceiling. 40 puts the reset at ~5 s, which has to sit comfortably above
 * the hold that starts a recording (~1 s) so healthy firmware always disarms first. */
#define POR_HOLD_TICKS 40

void por_arm(void)
{
    GPIO_EnablePorPin(BTN_PORT, BTN_PIN, GPIO_POR_PIN_POLARITY_LOW, POR_HOLD_TICKS);
}

void por_disarm(void)
{
    /* Zero in POR_PIN_SELECT is "no pin", the register's own reset value — the same
     * thing the SDK's GPIO_POR_PIN_REG macro yields for an illegal pin. */
    SetWord16(POR_PIN_REG, POR_PIN_REG_RESET);
}

void periph_init(void)
{
    /*
     * Arm the watchdog up front (~2 s at WATCHDOG_DEFAULT_PERIOD) so a hang anywhere
     * in periph_init — set_pad_functions, the DCDC bring-up — trips the NMI handler
     * (-> enter_failsafe -> BLE recovery) instead of a silent lockup. The SDK only
     * (re)starts the watchdog AFTER periph_init returns (arch_system.c), leaving this
     * window uncovered; on this ring SWD is a last-resort teardown, so closing the
     * window is what keeps the worst case ("hangs before ble_init") recoverable
     * over the air. Needs CFG_DEVELOPMENT_DEBUG OFF (it is) — otherwise the SDK
     * freezes the watchdog to wait for a debugger. Harmless if the ROM already armed
     * it: this just reloads the counter and resumes.
     */
    wdg_reload(WATCHDOG_DEFAULT_PERIOD);
    wdg_resume();

    /* P0_0 is SPI on both boards, not HW reset. */
    GPIO_Disable_HW_Reset();

    /*
     * DC-DC: try buck, fall back to boost, never hard-fail.
     *
     * This used to be `if (buck != 0) __BKPT(0);`. syscntl_dcdc_turn_on_in_buck
     * returns SYSCNTL_DCDC_ERR_NOT_IN_BUCK exactly when ANA_STATUS_REG.BOOST_SELECTED
     * is set (syscntl.c:224-227) — that is, when the board is wired for boost. On a
     * Cortex-M0+ with no debugger attached BKPT escalates to a HardFault, so a
     * boost-wired ring would hang right here: before ble_init, with a CRC-valid
     * image, so the failsafe never engages and only SWD gets it back.
     *
     * Which mode this ring uses is NOT established. // CONFIRM:
     * The stock firmware does not gamble either — its arch_main reads ANA_STATUS_REG
     * (0x5000002A), tests BOOST_SELECTED (0x0100) and drives DCDC_CTRL_REG
     * (0x50000080) down two different paths. Every SDK example uses boost at 3V0;
     * the buck/1V1 this template shipped with was the odd one out.
     *
     * ponytail: nothing handled past the fallback. If neither mode engages the SoC
     *           still runs off its LDO — degraded, but advertising, and advertising
     *           is what recovery needs. Hanging is the only outcome worth ruling out.
     */
    if (syscntl_dcdc_turn_on_in_buck(SYSCNTL_DCDC_LEVEL_1V1) != SYSCNTL_DCDC_ERR_NO_ERROR)
    {
        syscntl_dcdc_turn_on_in_boost(SYSCNTL_DCDC_LEVEL_3V0);
    }

    patch_func();
    set_pad_functions();
    GPIO_set_pad_latch_en(true);
}
