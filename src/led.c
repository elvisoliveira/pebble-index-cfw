/* RGB LED pattern player — see include/led.h for the byte format and its provenance. */
#include <led.h>
#include <board_config.h>
#include <gpio.h>
#include <app_easy_timer.h>
#include <string.h>

#define STEP_DURATION  0x1F   /* bits 4:0 */
#define STEP_CHANNELS  5      /* bits 7:5, shift */
#define UNIT_TU        5      /* 5 timer units of 10 ms = 50 ms per duration unit */

_Static_assert(LED_STEP(7, 31) == 0xFF, "step encoding: mask and duration must not overlap");
_Static_assert(LED_STEP(7, 31) >> STEP_CHANNELS == 7, "channel mask must land in bits 7:5");

static const GPIO_PIN led_pin[LED_CHANNELS] = { LED_A_PIN, LED_B_PIN, LED_C_PIN };

static uint8_t pattern[LED_PATTERN_MAX];
static uint8_t pattern_len;
static uint8_t step;
static timer_hnd led_timer = EASY_TIMER_INVALID_TIMER;

/* Drive the three channels from a 3-bit mask (bit0 = channel A).
 *
 * "Off" is INPUT, not OUTPUT-low, exactly as the ring does it (FUN_07fc4f20): a
 * high-Z pad neither leaks through the LED nor fights it. */
static void led_set(uint8_t mask)
{
    for (uint8_t i = 0; i < LED_CHANNELS; i++) {
        bool on = (mask >> i) & 1;
        GPIO_ConfigurePin(LED_PORT, led_pin[i], on ? OUTPUT : INPUT, PID_GPIO, on);
    }
}

static void led_run(void);

static void led_advance(void)
{
    led_timer = EASY_TIMER_INVALID_TIMER;
    step++;
    led_run();
}

static void led_run(void)
{
    uint8_t s = (step < pattern_len) ? pattern[step] : 0;
    uint8_t units = s & STEP_DURATION;

    /* Duration 0 ends the pattern — that is what makes 0x00 a terminator, and it is
     * the same test the ring's step advance makes before playing a step. */
    if (units == 0) {
        led_set(0);
        return;
    }
    led_set(s >> STEP_CHANNELS);
    led_timer = app_easy_timer(units * UNIT_TU, led_advance);
}

void led_play(const uint8_t *steps, uint8_t len)
{
    if (steps == NULL || len == 0 || len > LED_PATTERN_MAX) {
        return;
    }
    led_off();
    memcpy(pattern, steps, len);
    pattern_len = len;
    step = 0;
    led_run();
}

void led_off(void)
{
    if (led_timer != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(led_timer);
        led_timer = EASY_TIMER_INVALID_TIMER;
    }
    pattern_len = 0;
    step = 0;
    led_set(0);
}
