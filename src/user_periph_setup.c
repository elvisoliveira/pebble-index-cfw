/**
 ****************************************************************************************
 *
 * @file user_periph_setup.c
 *
 * @brief Peripherals setup and initialization.
 *
 * Copyright (c) 2015-2019 Dialog Semiconductor. All rights reserved.
 *
 * This software ("Software") is owned by Dialog Semiconductor.
 *
 * By using this Software you agree that Dialog Semiconductor retains all
 * intellectual property and proprietary rights in and to this Software and any
 * use, reproduction, disclosure or distribution of the Software without express
 * written permission or a license agreement from Dialog Semiconductor is
 * strictly prohibited. This Software is solely for use on or in conjunction
 * with Dialog Semiconductor products.
 *
 * EXCEPT AS OTHERWISE PROVIDED IN A LICENSE AGREEMENT BETWEEN THE PARTIES, THE
 * SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. EXCEPT AS OTHERWISE
 * PROVIDED IN A LICENSE AGREEMENT BETWEEN THE PARTIES, IN NO EVENT SHALL
 * DIALOG SEMICONDUCTOR BE LIABLE FOR ANY DIRECT, SPECIAL, INDIRECT, INCIDENTAL,
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THE SOFTWARE.
 *
 ****************************************************************************************
 */
#include <user_periph_setup.h>
#include <datasheet.h>
#include <system_library.h>
#include <rwip_config.h>
#include <syscntl.h>
#include <gpio.h>
#include <arch_wdg.h>

#include <debug.h>

#if DEVELOPMENT_DEBUG
void GPIO_reservations(void)
{
}
#endif

static void set_pad_functions(void)
{
    /* Active-low button: without a pull-up the line doesn't return to HIGH on release,
     * the rising edge vanishes and the wkupct one-shot re-arm dies after the 1st click
     * (counter stuck at 1 on the ring). It MUST live here, not in app_on_init: periph_init
     * re-runs on every wake from extended sleep; app_on_init runs only once. Ring: P0_1; in
     * the KIT_BUTTON_TEST build the button is P0_11. (INPUT_PULLUP/PID_GPIO: gpio.h, already included.) */
#ifdef KIT_BUTTON_TEST
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_11, INPUT_PULLUP, PID_GPIO, false);
    /* KIT hibernation-wake test: keep P0_1 HIGH (pull-up) at hibernation entry so the
     * clockless-wakeup polarity auto-detects "wake on LOW"; the jumper P0_1->GND is then
     * the wake edge (mirrors the ring, whose button IS P0_1). Not shipped. */
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_1,  INPUT_PULLUP, PID_GPIO, false);
#else
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_1,  INPUT_PULLUP, PID_GPIO, false);
#endif
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

    // Disable HW RST on P0_0
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

    // ROM patch
    patch_func();

    // Set pad functionality
    set_pad_functions();

    // Enable the pads
    GPIO_set_pad_latch_en(true);

#ifdef DEBUG_SEGGER
    static bool isSeggerInitialized = false;
    if (!isSeggerInitialized)
    {
        // Set up JLink RTT
        SEGGER_RTT_Init();
        DEBUG_PRINT_STRING("periph_init()\r\n");

        isSeggerInitialized = true;
    }
#endif
}
