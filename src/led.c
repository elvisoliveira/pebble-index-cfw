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
 * It also gates every write to the pads, but that buys tidiness, not SWD: the pad
 * latch call at the end of periph_init already hands P0_2/P0_10 to GPIO on every boot
 * and every wake, LED or no LED (bench-verified — see the pinout note in
 * board_config.h). Keying led_off and led_reapply on "is anything lit" just spares
 * three pad writes on paths that run often and would change nothing.
 */
static uint8_t led_mask;

/* Drive the three channels from a 3-bit mask (bit0 = channel A).
 *
 * "Off" is INPUT, not OUTPUT-low, exactly as the ring does it (FUN_07fc4f20): a
 * high-Z pad neither leaks through the LED nor fights it.
 *
 * A pad's HIGH level is VBAT_HIGH, so how bright a channel gets is a rail question,
 * not a GPIO one. Green and blue have Vf around 3 V and barely conduct below it, which
 * is why the stock firmware refcounts the DC-DC and holds VBAT_HIGH at
 * SYSCNTL_DCDC_LEVEL_3V0 for as long as any channel is lit. We do not: periph_init
 * already brings the DC-DC up in boost at 3.0 V, and on the kit VBAT_HIGH is USB's
 * 3.3 V. It would matter on a buck-wired board, where VBAT_HIGH is the raw battery —
 * red might light and green and blue not. Read that as a rail symptom, not miswiring. */
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
    /* Guarded like every other led_set caller: a click landing mid-loop would run
     * led_play -> led_set(new mask) on all three pads, and the resumed loop here would
     * then write the OLD mask over the remaining channels — pads split between two
     * masks, with led_mask recording only the newer one. */
    GLOBAL_INT_DISABLE();
    if (led_mask != 0) {
        led_set(led_mask);
    }
    GLOBAL_INT_RESTORE();
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

/*
 * Play the current step and schedule the next; its own timer callback. Runs in TASK
 * context (kernel timer dispatch), which the wkupct ISR can preempt at any line.
 *
 * THIS guard, not the one in led_play, is what closes the race the two share:
 * interrupts preempt task code, never the other way round, so an ISR-side critical
 * section runs only after the damage window has already passed. Unguarded, a click
 * landing between the handle clear below and the reassignment at the bottom lets the
 * resumed led_run overwrite the timer that click's led_play just scheduled — an
 * orphaned handle led_off can never cancel, firing later against the wrong pattern.
 *
 * One hair-width window remains that no code here can close, and it costs more than
 * a wasted cancel: between the kernel invoking this callback and its first line, a
 * click runs led_play to completion — new pattern, step 0 lit, its own timer stored.
 * The stale led_run then resumes, clears that timer (orphaning it) and reads step 1,
 * which is the terminator, so it darkens the blink the click just started. Net effect
 * is a dropped blink, plus an orphan that fires later — against a finished pattern,
 * where it finds nothing to do, or against a live one, where it truncates that blink
 * and spawns one more orphan. Bounded either way, and only ever cosmetic: no path
 * leaves a channel lit, because every exit here ends in led_set(0) or a scheduled
 * advance. Closing it needs per-callback identity, which app_easy_timer
 * does not offer: a generation counter cannot work, because the orphan would read the
 * same global as the live run. Two trampoline callbacks would, at the cost of being
 * ugly for a window a few instructions of kernel dispatch wide.
 */
static void led_run(void)
{
    GLOBAL_INT_DISABLE();
    led_timer = EASY_TIMER_INVALID_TIMER;   /* no-op when called from led_play */
    uint8_t s = (step < pattern_len) ? pattern[step++] : 0;
    uint8_t units = s & LED_STEP_DUR_MASK;

    if (units == 0) {
        /* Duration 0 ends the pattern — that is what makes 0x00 a terminator, and it
         * is the same test the ring's step advance makes before playing a step. */
        led_set(0);
    } else {
        led_set(s >> LED_STEP_CH_SHIFT);
        led_timer = app_easy_timer(units * UNIT_TU, led_run);
        if (led_timer == EASY_TIMER_INVALID_TIMER) {
            /* Out of timer slots (they are a fixed pool, and a cancelled one stays
             * taken until its message is processed). Nothing will advance this step,
             * so go dark rather than hold a channel lit for the rest of the burst. */
            led_set(0);
        }
    }
    /* Single exit on purpose: DISABLE/RESTORE are a brace pair — an early return
     * between them would leave interrupts off for good. */
    GLOBAL_INT_RESTORE();
}

/*
 * led_play runs in INTERRUPT context today — on_wakeup is the wkupct callback — while
 * led_run and the led_off() at burst end run in task context, and all three touch
 * led_timer, pattern and step. The guard that actually closes their shared race lives
 * in led_run (see there): an ISR cannot be preempted by the task code it races with,
 * so on the current call path this critical section defends nothing. It stays because
 * it costs two instructions and makes led_play safe from ANY context — a future
 * task-context caller (a GATT-triggered pattern, say) would otherwise silently reopen
 * the race. Nesting over led_run's own guard is fine: each pair keeps its own PRIMASK
 * copy in its own scope.
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
