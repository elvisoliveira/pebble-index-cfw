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
 *                      (P0_3 is really the mic's VCC — see the ring branch below)
 *   RGB LED A/B/C      P0_2/P0_8/P0_10         P0_9/P0_8/P0_6 (J4 "PWM"/"SDA"/"RX")
 *   microphone in      P0_7                    P0_7 (J3 "CS") — same pin on both
 *   microphone power   P0_3 driven HIGH        none (bench module runs off J3 "3V3")
 *
 * The pin numbers were read out of the ring's own factory firmware (2026-08-10), not
 * guessed. DC-DC is NOT here: periph_init auto-detects buck->boost, same code both boards.
 */
#include <gpio.h>

#define BTN_PORT     GPIO_PORT_0
#define FLASH_PORT   GPIO_PORT_0
#define LED_PORT     GPIO_PORT_0
#define MIC_PORT     GPIO_PORT_0

#ifdef TARGET_KIT
    /* P0_11 is SW2's pin (bench-validated 2026-08-27: the counter tracked clicks 1:1).
     * An external momentary button wired to J4 "INT" sits in PARALLEL with SW2 — both
     * only close the pin to ground — so you get the comfortable button and SW2 keeps
     * working. Nothing on this build ever drives P0_11 as an output, so SW2's 270 R to
     * ground is harmless. */
    #define BTN_PIN        GPIO_PIN_11
    #ifdef KIT_BTN_EXT
        #error "KIT_BTN_EXT was removed: the kit button is P0_11 (J4 INT, in parallel with SW2). Drop the flag."
    #endif
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
     * stuck high, because the J-Link OB's virtual COM port owns it. P0_6 (J4 "RX") is
     * that same VCOM's other half, yet the OB leaves it alone — channel C lights and
     * goes dark cleanly on the bench. That leaves the kit with exactly five usable pins
     * for five jobs, so the assignment is a permutation,
     * not a choice. This one is the good permutation: it keeps P0_7 for the microphone,
     * which is the pin the ring uses too, and it keeps every output off SW2's pin. */
    #define LED_A_PIN      GPIO_PIN_9   /* J4 "PWM" */
    #define LED_B_PIN      GPIO_PIN_8   /* J4 "SDA" — same channel, same pin as the ring */
    #define LED_C_PIN      GPIO_PIN_6   /* J4 "RX"  */
    /* Microphone: a MAX9814 module on J3 "3V3"/GND, output into P0_7, powered whenever
     * the board is (no enable pin). Numbers below from the datasheet, Rev 3 8/16.
     *
     * MICOUT rests at 1.23 V TYP, and the part spread is 1.14-1.32 V — not a fixed
     * value, which is why mic_capture measures the bias instead of assuming one. Ours
     * reads 1.229 V (bench 2026-09-02). The swing is 2 x V_TH, set by the module's own
     * divider off the 2.0 V MICBIAS: the datasheet's reference divider gives 1.6 Vpp
     * and its spec example 1.4 Vpp, so the signal lives roughly in 0.4-2.0 V. Either
     * way it clears the ring's 0-1.8 V window, which is what the wider attenuator here
     * is for; the exact V_TH is measurable at the module's TH pin if it is ever needed.
     *
     * GAIN and A/R are both left floating. GAIN floating is 60 dB. A/R floating is
     * 1:4000 — the LONGEST attack-to-release ratio of the three, not a middle setting
     * (Table 1: GND 1:500, VDD 1:2000, unconnected 1:4000). With the reference 470 nF
     * timing cap that is 1.1 ms attack, 30 ms hold (fixed), 4400 ms release.
     *
     * Two consequences for anything that reads this microphone:
     *
     *   - mic_read's burst is ~4 ms, FOUR attack times. The AGC has already compressed
     *     before the burst ends, so pp is always an AGC-flattened number. It is the
     *     attack that does this, not the release.
     *   - after any loud sound the gain stays down for ~4.4 s, so two clicks in a row
     *     do not produce comparable levels. Not a dead microphone; the instrument
     *     recovering.
     *
     * THE RING HAS NO AGC AT ALL. So the pumping this module adds to a recording — the
     * gain ducking on a loud syllable and taking seconds to come back, which the
     * datasheet names "pumping" or "breathing" — is a property of the BENCH, not of the
     * product. Do not tune the capture path against it. If a flat reference is ever
     * wanted, the datasheet's own switch is TH tied to MICBIAS, which disables the AGC
     * outright. What the two boards DO share is that neither has any anti-aliasing:
     * both are flat well past the ~11 kHz the converter free-runs at. */
    #define MIC_ADC_ATTN   ADC_INPUT_ATTN_3X   /* 0-2.7 V */
#else
    #define BTN_PIN        GPIO_PIN_1
    #define FLASH_EN_PIN   GPIO_PIN_9   /* SPI CS  — FUNC_SPI_CSN0, cs_pad.pin */
    #define FLASH_CLK_PIN  GPIO_PIN_0   /* SPI CLK — FUNC_SPI_CLK */
    #define FLASH_DO_PIN   GPIO_PIN_6   /* SPI DO  — FUNC_SPI_DO */
    #define FLASH_DI_PIN   GPIO_PIN_11  /* SPI DI  — FUNC_SPI_DI */
    /* Power enables the stock app raises before touching the flash. P0_4 is the flash's
     * own VCC; P0_3 is the MICROPHONE's (2026-08-28, from the stock app's refcounted
     * rail: 0=LED at 3.0 V, 1=flash at 1.8 V, 2=mic at 1.8 V) — it is listed here
     * because the stock app raises it too, and raising a microphone to read flash costs
     * nothing. Kept rather than dropped so this matches what the ring's own code does;
     * if it is ever dropped, note that mic_read() drives P0_3 LOW after every click, so
     * flash_on() would then be the only thing putting it back. */
    #define FLASH_HAS_PWR_PINS
    #define FLASH_PWR1_PIN GPIO_PIN_4   /* flash VCC */
    #define FLASH_PWR2_PIN GPIO_PIN_3   /* mic VCC — same pin as MIC_PWR_PIN below */
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
    /* Microphone: the analog MEMS marked R61E G31H, powered from P0_3 and read on P0_7.
     * The stock app drives P0_3 high, waits 50 us and converts — both numbers read out
     * of app v3.74 (its adc_config template at 0x07fc6b34), and both kept here.
     *
     * The attenuator is the one number NOT copied from the ring, and the reason is the
     * rail. A pad's HIGH is VBAT_HIGH, so P0_3 hands the microphone whatever the DC-DC
     * is holding; the stock app refcounts that rail and drops it to 1.8 V for the mic,
     * which is what makes its 0-1.8 V window exactly right. This CFW does not refcount
     * — periph_init brings the DC-DC up once and leaves it, at 3.0 V in boost (see
     * led.c, where the LED is why). A 3.0 V rail puts the MEMS at rest near 1.5 V, or
     * 83% of a 1.8 V scale, with 0.3 V of headroom left above it: the positive half of
     * every waveform clipped at the rail.
     *
     * 0-3.6 V costs one bit of resolution and buys independence from the rail: the
     * mic rests mid-window whether VBAT_HIGH is 1.8 V or 3.0 V, so this stays right if
     * the refcount is ever ported. Cheaper than porting it to fix a recording. */
    #define MIC_ADC_ATTN   ADC_INPUT_ATTN_4X   /* 0-3.6 V — see above, NOT the ring's 2x */
    #define MIC_HAS_PWR_PIN
    #define MIC_PWR_PIN    GPIO_PIN_3
    #define MIC_PWR_SETTLE_US 50
#endif

/* The ADC input is the ONE thing the microphone does not differ on: P0_7 is single-ended
 * channel 3 on both boards. Kept out of the branches above so that stays visible.
 *
 * These expand to adc.h enums, so only a file that includes <adc.h> may use them —
 * mic.c does, and nothing else needs to. */
#define MIC_ADC_INPUT      ADC_INPUT_SE_P0_7

#endif // BOARD_CONFIG_H_
