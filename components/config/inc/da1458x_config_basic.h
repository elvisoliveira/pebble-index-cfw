/* da1458x_config_basic.h — basic compile configuration (from the Dialog SDK template). */

#ifndef _DA1458X_CONFIG_BASIC_H_
#define _DA1458X_CONFIG_BASIC_H_

/* The CUSTS1 custom server is not optional. It carries the Control Point (write 0x00 ->
 * enter_failsafe, the button-independent way back) AND the audio characteristic the clip
 * transfer notifies on, so the app depends on it in both directions. It used to sit
 * behind a WITH_CTRL_POINT flag offering a "lean beacon-only build"; that build stopped
 * linking once the recording landed, so the flag was a promise, not a switch. Enabled
 * unconditionally in user_profiles_config.h and user_modules_config.h. */

#include "da1458x_stack_config.h"
#include "user_profiles_config.h"

/* Integrated processor mode: the host app runs on-chip as the TASK_APP kernel task
 * (undefined = external processor over GTL). */
#define CFG_APP

/* BLE security (pairing/bonding) OFF: the click model needs none — dropping it removes
 * the app_security module and the security callbacks (BLE_APP_SEC gates them in
 * user_callback_config.h). */
/* #define CFG_APP_SECURITY */

/* Watchdog on. */
#define CFG_WDOG

/* Max concurrent connections (sizes the heap; 1 is the peripheral-role optimum). */
#define CFG_MAX_CONNECTIONS     (1)

/*
 * Development/debug mode: OFF for the ring. With it on the SDK breaks into a breakpoint
 * in the HardFault and NMI (watchdog) handlers — HardFault_HandlerC additionally calls
 * wdg_freeze() before spinning. On a ring with no debugger attached that is a permanent
 * hang with the watchdog stopped: a CRC-valid image, so the failsafe never engages and
 * only SWD gets it back. With this off, HardFault_HandlerC takes the production path
 * (wdg_reload(1), "force execution of NMI Handler") and our NMI_Handler in
 * src/interrupts.c drops the ring into the failsafe instead.
 *
 * Re-enable it for devkit bring-up, where a debugger IS attached and breaking on a
 * fault is what you want. Never ship it to the ring.
 */
#undef CFG_DEVELOPMENT_DEBUG

/* No UART console. */
#undef CFG_PRINTF
#undef CFG_UART1_SDK

/* No SDK-managed external storage (recovery drives the SPI flash directly, user_app.c). */
#undef CFG_SPI_FLASH_ENABLE
#undef CFG_I2C_EEPROM_ENABLE

/* No DMA support on any interface. */
#undef CFG_UART_DMA_SUPPORT
#undef CFG_SPI_DMA_SUPPORT
#undef CFG_I2C_DMA_SUPPORT
#undef CFG_ADC_DMA_SUPPORT

/* Not in fixed Bypass power mode. */
#undef CFG_POWER_MODE_BYPASS

#endif // _DA1458X_CONFIG_BASIC_H_
