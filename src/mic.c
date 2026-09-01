/* Microphone level over the GP_ADC — see include/mic.h. */
#include <adc.h>            /* before board_config.h: MIC_ADC_* expand to its enums */
#include <mic.h>
#include <board_config.h>
#include <gpio.h>
#include <arch.h>           /* arch_asm_delay_us */

/*
 * One burst. 64 conversions at 64x oversampling is about 4 ms, which spans a couple of
 * cycles of anything above ~250 Hz — enough of a voice to see a peak.
 *
 * ponytail: this blocks, and its caller is the button ISR. 4 ms there is well inside
 *           what this app already does in that context (enter_failsafe erases a flash
 *           sector), and a level is a once-per-click measurement, not a stream. The
 *           streaming path has no reason to block at all: it is three DMA channels and
 *           no CPU. Build that when a recording is wanted, not to make this faster.
 */
#define MIC_BURST 64

/* Verbatim the ring's own template (app v3.74, 0x07fc6b34) except the attenuator,
 * which is a board difference. Continuous mode is off: the stock app leaves it off too
 * and clocks conversions externally, and here every conversion is asked for. */
static const adc_config_t mic_cfg = {
    .input_mode       = ADC_INPUT_MODE_SINGLE_ENDED,
    .input            = MIC_ADC_INPUT,
    .smpl_time_mult   = 1,
    .continuous       = false,
    .interval_mult    = 0,
    .input_attenuator = MIC_ADC_ATTN,
    .chopping         = true,
    .oversampling     = 6,      /* 2^6 = 64 averaged per conversion, so 16-bit results */
};

mic_reading_t mic_read(void)
{
#ifdef MIC_HAS_PWR_PIN
    /* The ring switches the microphone's supply from a GPIO, and the stock app waits
     * before converting — an unsettled bias reads as a huge fake peak. */
    GPIO_ConfigurePin(MIC_PORT, MIC_PWR_PIN, OUTPUT, PID_GPIO, true);
    arch_asm_delay_us(MIC_PWR_SETTLE_US);
#endif
    adc_init(&mic_cfg);

    uint16_t lo = 0xFFFF, hi = 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < MIC_BURST; i++) {
        uint16_t s = adc_get_sample();
        if (s < lo) lo = s;
        if (s > hi) hi = s;
        sum += s;
    }

    adc_disable();
#ifdef MIC_HAS_PWR_PIN
    GPIO_ConfigurePin(MIC_PORT, MIC_PWR_PIN, OUTPUT, PID_GPIO, false);
#endif
    mic_reading_t r = { .pp = hi - lo, .dc = (uint16_t)(sum / MIC_BURST) };
    return r;
}
