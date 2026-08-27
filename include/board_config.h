#ifndef BOARD_CONFIG_H_
#define BOARD_CONFIG_H_

/*
 * Device differentiation — the ONE place the ring and the DA14535 kit differ.
 *
 * Default (nothing defined) = the shipping RING (FCGQFN24). Define TARGET_KIT (via
 * -DKIT_DEFS=TARGET_KIT) for the DA14535 USB devkit bench build. Everything else in the
 * CFW is shared between the two boards.
 *
 *                      ring (default)          renesas kit (TARGET_KIT)
 *   button             P0_1                    P0_11 = SW2 on-board (default), or P0_7
 *                                              external (MikroBUS J3 pin 3, btn->GND) with
 *                                              -DKIT_DEFS="TARGET_KIT;KIT_BTN_EXT"
 *   flash CS/CLK/DO/DI  P0_9/P0_0/P0_6/P0_11    P0_1/P0_4/P0_0/P0_3 (the kit's own AT25XE021A)
 *   flash power pins   P0_4, P0_3 driven       none (those pins ARE the kit's SPI CLK/MISO)
 *
 * The pin numbers were read out of the ring's own factory firmware (2026-08-10), not
 * guessed. DC-DC is NOT here: periph_init auto-detects buck->boost, same code both boards.
 */
#include <gpio.h>

#define BTN_PORT     GPIO_PORT_0
#define FLASH_PORT   GPIO_PORT_0

#ifdef TARGET_KIT
    /* SW2 (270R to GND, no external pull-up, pad reset default pull-DOWN) is the default:
     * with INPUT_PULLUP on BTN_PIN it counts clicks 1:1 (bench-validated 2026-08-27).
     * KIT_BTN_EXT swaps in a ring-like external button on P0_7 — a deliberate option,
     * not a fix; SW2 was never at fault. (The "counter stuck at 1 click" that once showed
     * up on the kit was the wkupct re-arm race, fixed in a541992, independent of the pin.) */
    #ifdef KIT_BTN_EXT
        #define BTN_PIN    GPIO_PIN_7   /* external momentary btn, MikroBUS J3 pin 3 -> GND */
    #else
        #define BTN_PIN    GPIO_PIN_11  /* SW2, the kit's on-board user button (default) */
    #endif
    #define FLASH_EN_PIN   GPIO_PIN_1   /* CS   */
    #define FLASH_CLK_PIN  GPIO_PIN_4   /* CLK  */
    #define FLASH_DO_PIN   GPIO_PIN_0   /* MOSI */
    #define FLASH_DI_PIN   GPIO_PIN_3   /* MISO */
    /* no FLASH_HAS_PWR_PINS: on the kit P0_4/P0_3 are the SPI CLK/MISO, not power enables */
#else
    #define BTN_PIN        GPIO_PIN_1
    #define FLASH_EN_PIN   GPIO_PIN_9   /* SPI CS  — FUNC_SPI_CSN0, cs_pad.pin */
    #define FLASH_CLK_PIN  GPIO_PIN_0   /* SPI CLK — FUNC_SPI_CLK */
    #define FLASH_DO_PIN   GPIO_PIN_6   /* SPI DO  — FUNC_SPI_DO */
    #define FLASH_DI_PIN   GPIO_PIN_11  /* SPI DI  — FUNC_SPI_DI */
    #define FLASH_HAS_PWR_PINS          /* ring: drive P0_4/P0_3 HIGH to power the flash */
    #define FLASH_PWR1_PIN GPIO_PIN_4
    #define FLASH_PWR2_PIN GPIO_PIN_3
#endif

#endif // BOARD_CONFIG_H_
