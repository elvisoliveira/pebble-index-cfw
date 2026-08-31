/* RGB LED pattern player — see include/led.h for the byte format and its provenance. */
#include <led.h>
#include <board_config.h>
#include <gpio.h>
#include <app_easy_timer.h>
#include <ll.h>             /* GLOBAL_INT_DISABLE / GLOBAL_INT_RESTORE */
#include <string.h>

#define UNIT_TU  MS_TO_TIMERUNITS(LED_UNIT_MS)   /* timer units per duration unit */

static const GPIO_PIN led_pin[LED_CHANNELS] = { LED_A_PIN, LED_B_PIN, LED_C_PIN };

static uint8_t pattern[LED_PATTERN_MAX];
static uint8_t pattern_len;
static uint8_t step;
static timer_hnd led_timer = EASY_TIMER_INVALID_TIMER;

/*
 * What led_set last drove. This is the one piece of LED state that has to outlive a
 * sleep, and it does: the build retains all RAM.
 *
 * It also gates every write to the pads. On the ring channels A and C are P0_2 and
 * P0_10 — the SWD pads — so an image whose LED is dark must not reprogram them, not
 * even to "turn off": going dark is an active remux, not a no-op. Keying on "is
 * anything lit" means a unit that never blinks never touches its debug pads.
 */
static uint8_t led_mask;

/* Drive the three channels from a 3-bit mask (bit0 = channel A).
 *
 * "Off" is INPUT, not OUTPUT-low, exactly as the ring does it (FUN_07fc4f20): a
 * high-Z pad neither leaks through the LED nor fights it. */
static void led_set(uint8_t mask)
{
    led_mask = mask;
    for (uint8_t i = 0; i < LED_CHANNELS; i++) {
        bool on = (mask >> i) & 1;
        GPIO_ConfigurePin(LED_PORT, led_pin[i], on ? OUTPUT : INPUT, PID_GPIO, on);
    }
}

/*
 * Re-drive the lit channels after an extended-sleep wake.
 *
 * The peripheral domain loses its GPIO configuration in extended sleep. periph_init
 * re-runs on every wake and ends with GPIO_set_pad_latch_en(true), at which point the
 * pads follow their RESET mode registers — dark. Without this the LED dies at the
 * first sleep inside a pattern, and the chip sleeps between advertising events, so
 * that is every few tens of milliseconds: a 300 ms blink would show as a flicker.
 * Same reason the button pull-up lives in set_pad_functions() and not in app_on_init.
 */
void led_reapply(void)
{
    if (led_mask != 0) {
        led_set(led_mask);
    }
}

static void led_stop(void)
{
    if (led_timer != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(led_timer);
        led_timer = EASY_TIMER_INVALID_TIMER;
    }
    pattern_len = 0;
    step = 0;
}

/* Play the current step and schedule the next; its own timer callback. */
static void led_run(void)
{
    led_timer = EASY_TIMER_INVALID_TIMER;   /* no-op when called from led_play */
    uint8_t s = (step < pattern_len) ? pattern[step++] : 0;
    uint8_t units = s & LED_STEP_DUR_MASK;

    /* Duration 0 ends the pattern — that is what makes 0x00 a terminator, and it is
     * the same test the ring's step advance makes before playing a step. */
    if (units == 0) {
        led_set(0);
        return;
    }
    led_set(s >> LED_STEP_CH_SHIFT);

    led_timer = app_easy_timer(units * UNIT_TU, led_run);
    if (led_timer == EASY_TIMER_INVALID_TIMER) {
        /* Out of timer slots (they are a fixed pool, and a cancelled one stays taken
         * until its message is processed). Nothing will advance this step, so go dark
         * rather than hold a channel lit for the rest of the burst. */
        led_set(0);
    }
}

/*
 * led_play runs in INTERRUPT context — on_wakeup is the wkupct callback — while
 * led_run and the led_off() at burst end run in task context. Both touch led_timer,
 * pattern and step. Without the critical section a click landing inside led_run
 * between the handle being cleared and reassigned orphans a timer that led_off can
 * then never cancel, and it fires later against a different pattern. The window is a
 * few instructions, but rapid clicking is exactly the failsafe gesture.
 */
void led_play(const uint8_t *steps, uint8_t len)
{
    if (steps == NULL || len == 0 || len > LED_PATTERN_MAX) {
        return;
    }
    GLOBAL_INT_DISABLE();
    led_stop();
    memcpy(pattern, steps, len);
    pattern_len = len;
    step = 0;
    led_run();
    GLOBAL_INT_RESTORE();
}

void led_off(void)
{
    GLOBAL_INT_DISABLE();
    led_stop();
    if (led_mask != 0) {
        led_set(0);
    }
    GLOBAL_INT_RESTORE();
}
