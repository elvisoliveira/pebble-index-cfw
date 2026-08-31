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
 *   button             P0_1                    P0_11 (J4 "INT") — SW2's pin, so an
 *                                              external button there sits in parallel
 *                                              with it and both work
 *   flash CS/CLK/DO/DI  P0_9/P0_0/P0_6/P0_11    P0_1/P0_4/P0_0/P0_3 (the kit's own AT25XE021A)
 *   flash power pins   P0_4, P0_3 driven       none (those pins ARE the kit's SPI CLK/MISO)
 *   RGB LED A/B/C      P0_2/P0_8/P0_10         P0_9/P0_8/P0_6 (J4 "PWM"/"SDA"/"RX")
 *   microphone in      P0_7                    P0_7 (J3 "CS") — same pin on both
 *
 * The pin numbers were read out of the ring's own factory firmware (2026-08-10), not
 * guessed. DC-DC is NOT here: periph_init auto-detects buck->boost, same code both boards.
 */
#include <gpio.h>

#define BTN_PORT     GPIO_PORT_0
#define FLASH_PORT   GPIO_PORT_0
#define LED_PORT     GPIO_PORT_0

#ifdef TARGET_KIT
    /* P0_11 is SW2's pin (bench-validated 2026-08-27: the counter tracked clicks 1:1).
     * An external momentary button wired to J4 "INT" sits in PARALLEL with SW2 — both
     * only close the pin to ground — so you get the comfortable button and SW2 keeps
     * working. Nothing on this build ever drives P0_11 as an output, so SW2's 270 R to
     * ground is harmless. */
    #define BTN_PIN        GPIO_PIN_11
    #define FLASH_EN_PIN   GPIO_PIN_1   /* CS   */
    #define FLASH_CLK_PIN  GPIO_PIN_4   /* CLK  */
    #define FLASH_DO_PIN   GPIO_PIN_0   /* MOSI */
    #define FLASH_DI_PIN   GPIO_PIN_3   /* MISO */
    /* no FLASH_HAS_PWR_PINS: on the kit P0_4/P0_3 are the SPI CLK/MISO, not power enables */
    /* RGB LED. Bench-confirmed on 2026-08-31 by driving each pin over SWD and looking:
     * every channel lights on a HIGH, same polarity as the ring. P0_9 also drives the
     * kit's own D7, so channel A blinks twice over.
     *
     * P0_5 (J4 "TX") looks like the obvious third pin and is NOT usable — it reads
     * stuck high, because the J-Link OB's virtual COM port owns it. That leaves the kit
     * with exactly five usable pins for five jobs, so the assignment is a permutation,
     * not a choice. This one is the good permutation: it keeps P0_7 for the microphone,
     * which is the pin the ring uses too, and it keeps every output off SW2's pin. */
    #define LED_A_PIN      GPIO_PIN_9   /* J4 "PWM" */
    #define LED_B_PIN      GPIO_PIN_8   /* J4 "SDA" — same channel, same pin as the ring */
    #define LED_C_PIN      GPIO_PIN_6   /* J4 "RX"  */
#else
    #define BTN_PIN        GPIO_PIN_1
    #define FLASH_EN_PIN   GPIO_PIN_9   /* SPI CS  — FUNC_SPI_CSN0, cs_pad.pin */
    #define FLASH_CLK_PIN  GPIO_PIN_0   /* SPI CLK — FUNC_SPI_CLK */
    #define FLASH_DO_PIN   GPIO_PIN_6   /* SPI DO  — FUNC_SPI_DO */
    #define FLASH_DI_PIN   GPIO_PIN_11  /* SPI DI  — FUNC_SPI_DI */
    #define FLASH_HAS_PWR_PINS          /* ring: drive P0_4/P0_3 HIGH to power the flash */
    #define FLASH_PWR1_PIN GPIO_PIN_4
    #define FLASH_PWR2_PIN GPIO_PIN_3
    /* RGB LED, read out of the stock app v3.74 (FUN_07fc4f20 drives exactly these three
     * as GPIO, HIGH to light). Which channel is which colour is NOT known: the firmware
     * only ever speaks in channels. Lighting one at a time on a real ring settles it.
     *
     * ⚠ P0_2 and P0_10 are also the SWD pins, and that is a PCB fact rather than a
     * choice: the flex routes the LED to those pads. The ring's only free pin is P0_5,
     * and it does not reach the LED, so there is no other pinout to pick.
     *
     * The LED does NOT cost SWD, though — the CFW already does, with or without it.
     * Bench, 2026-08-30: with SYS_CTRL_REG = 0x01A2, so DEBUGGER_ENABLE (0x0180) = 3 =
     * SWD_DATA_AT_P0_10, the debugger owned P0_2/P0_10 and SWD was alive; writing
     * PAD_LATCH_REG = 1 killed it. The port mode register wins over the debug mux once
     * the latch opens, and P0x_MODE_REG resets to 0x200 — INPUT, PID_GPIO. periph_init
     * ends with GPIO_set_pad_latch_en(true) and never configures those two pins, so
     * every boot and every extended-sleep wake hands them to GPIO. That is the
     * long-standing "bootable image on the kit flash = recurring SWD death".
     *
     * So SWD lives only in the window between reset and that latch call, which is
     * exactly what the bench recovery procedures rely on. Accepted 2026-08-31: the
     * LED changes nothing here, and SWD sits behind three recovery nets that do not
     * need it — 5 clicks, the NMI hook, and the boot counter. */
    #define LED_A_PIN      GPIO_PIN_2   /* ⚠ also SWDIO/SWCLK */
    #define LED_B_PIN      GPIO_PIN_8
    #define LED_C_PIN      GPIO_PIN_10  /* ⚠ also SWDIO/SWCLK */
#endif

#endif // BOARD_CONFIG_H_
