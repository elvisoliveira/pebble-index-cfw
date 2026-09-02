/* Microphone recording over the GP_ADC — see include/mic.h. */
#include <adc.h>            /* before board_config.h: MIC_ADC_* expand to its enums */
#include <mic.h>
#include <board_config.h>
#include <gpio.h>
#include <arch.h>           /* arch_asm_delay_us */
#include <adpcm.h>
#include <arch_wdg.h>       /* wdg_reload during the capture loop */

/*
 * One burst. 64 conversions at 64x oversampling is about 4 ms, which spans a couple of
 * cycles of anything above ~250 Hz — long enough to average a resting level out of a
 * voice. mic_capture opens with one to find the bias it will subtract.
 */
#define MIC_BURST 64

/* Running-DC filter pole, see the capture loop. Corner = rate / (2*pi * 2^shift): at
 * 8 kHz, shift 4 puts it near 80 Hz — under the lowest voice fundamental, far above the
 * bias drift it exists to follow. */
#define DC_SHIFT 4

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

/* Power/settle + ADC bracketing. Kept as its own pair rather than inlined into the
 * capture: on the ring MIC_PWR_PIN is P0_3, the flash's second power pin
 * (board_config.h), so this sequence is a shared-rail hardware invariant, and a
 * corrupted-first-samples bug here would reproduce only on a sealed ring, never on the
 * kit (which has no power pin). */
static void mic_on(void)
{
#ifdef MIC_HAS_PWR_PIN
    /* The ring switches the microphone's supply from a GPIO, and the stock app waits
     * before converting — an unsettled bias reads as a huge fake peak. */
    GPIO_ConfigurePin(MIC_PORT, MIC_PWR_PIN, OUTPUT, PID_GPIO, true);
    arch_asm_delay_us(MIC_PWR_SETTLE_US);
#endif
    adc_init(&mic_cfg);
}

static void mic_off(void)
{
    adc_disable();
#ifdef MIC_HAS_PWR_PIN
    GPIO_ConfigurePin(MIC_PORT, MIC_PWR_PIN, OUTPUT, PID_GPIO, false);
#endif
}

/*
 * The clip. 24 KB of nibbles is 49152 samples: 6.1 s at MIC_SAMPLE_RATE_HZ.
 *
 * That is as far as it goes on RAM alone. The linker leaves 5280 bytes between
 * __HeapLimit and __StackLimit (map, 2026-09-02) on top of the 1792 the stack reserves
 * for itself, and growing this buffer eats that gap. The gap is not free space to
 * spend: the reserved 1792 is a floor, not a measurement, so what actually protects the
 * stack is leaving room ABOVE it. Overshooting is at least loud — the linker script
 * asserts __StackLimit >= __HeapLimit and the link fails — but the assert only sees the
 * reservation, never how deep the call tree really goes.
 */
#define CLIP_BYTES   (24 * 1024)
#define CLIP_SAMPLES (CLIP_BYTES * 2)

static uint8_t clip[CLIP_BYTES];
static uint16_t clip_samples;

uint16_t mic_capture(bool (*keep)(void))
{
    adpcm_state_t st;
    adpcm_reset(&st);
    mic_on();

#ifdef MIC_WARMUP_MS
    /* Let the microphone finish powering up before anything is measured off it — see
     * board_config.h for where the number comes from. Blocking is free here: the caller
     * is about to block for seconds anyway, and the recording light is already on, so
     * the wait reads as "starting". The ceiling is the watchdog, ~2 s, and 150 ms
     * leaves that untouched; feed it from here if this ever grows. */
    arch_asm_delay_us(MIC_WARMUP_MS * 1000);
#endif

    /*
     * Where the signal actually rests, measured, instead of assuming mid-scale.
     *
     * Mid-scale is only right when the microphone's bias sits at half the ADC's window,
     * and neither board manages it: the MAX9814 rests at 1.23 V typical under a 0-2.7 V
     * window — and typical is the word, since the part spread is 1.14-1.32 V, so this
     * was never a constant to hardcode — while the ring's MEMS follows VBAT_HIGH under
     * a window that does not (see board_config.h). The kit's rest measured about -3151
     * counts off mid-scale, and ADPCM is differential: a constant offset the encoder
     * does not know about is a step it has to slew through from predictor 0, which is
     * the click at the start of a clip. One 4 ms burst and the offset is gone.
     *
     * The number is also the signal path's health check, if it is ever read out. A
     * MAX9814 rests at 1.23 V typical (1.14-1.32 V across parts), about 29800 counts
     * under the kit's 3x attenuator; ours measured 29835. The ring's MEMS rests near half
     * of VBAT_HIGH. A bias near ZERO means nothing is arriving at the pin — wiring,
     * power, the wrong pad, or a module built to the datasheet with an output coupling
     * capacitor, which strips exactly the offset this ADC needs as its midpoint. A
     * plausible bias with a flat recording means something is compressing the signal,
     * which on the kit is the AGC's job description (board_config.h, the A/R pin).
     */
    int32_t bias = 0;
    for (uint8_t i = 0; i < MIC_BURST; i++) {
        bias += adc_get_sample();
    }
    bias /= MIC_BURST;

    /*
     * Running DC, Q8, one pole. The bias burst above is a snapshot and the bias MOVES:
     * a real ring, holding still, put 98.6% of a silent clip's energy below 50 Hz, and
     * 69% of a clip of clear speech — drift, not sound, since voice has no fundamental
     * down there. Subtracting a constant cannot follow that.
     *
     * It costs more than it looks. ADPCM sizes its step from the whole signal, so a
     * predictor busy tracking drift spends its range on the drift and quantises the
     * speech coarsely. The rumble is not merely audible; it is taking resolution away
     * from the voice underneath it.
     *
     * DC_SHIFT sets the corner (see its definition). Starts at zero because the samples
     * are already bias-corrected, so there is no settling transient of its own.
     */
    int32_t dc_q8 = 0;

    uint16_t n = 0;         /* samples STORED, at MIC_SAMPLE_RATE_HZ */
    int32_t sum = 0;        /* the running mean that stands in for a low-pass filter */
    uint16_t count = 0;
    uint16_t phase = 0;

    while (n < CLIP_SAMPLES && keep()) {
        /* The ADC is unsigned; ADPCM wants a signed swing about zero. Clamped because
         * the bias is a measurement, not the midpoint: a rail-to-rail excursion away
         * from it can exceed int16, and wrapping would invert that sample — the same
         * crack adpcm_encode's own clamp exists to avoid. */
        int32_t v = (int32_t)adc_get_sample() - bias;
        if (v > 32767) {
            v = 32767;
        } else if (v < -32768) {
            v = -32768;
        }
        sum += v;
        count++;

        /* Bresenham on the two rates: emit whenever the output clock has caught up with
         * the input one, which spreads the kept samples evenly instead of bunching them. */
        phase += MIC_SAMPLE_RATE_HZ;
        if (phase < MIC_SOURCE_RATE_HZ) {
            continue;
        }
        phase -= MIC_SOURCE_RATE_HZ;

        int32_t avg = sum / (int32_t)count;
        dc_q8 += ((avg << 8) - dc_q8) >> DC_SHIFT;
        uint8_t nibble = adpcm_encode(&st, (int16_t)(avg - (dc_q8 >> 8)));
        sum = 0;
        count = 0;

        uint8_t *slot = &clip[n >> 1];
        *slot = (n & 1) ? (uint8_t)(*slot | (nibble << 4)) : nibble;
        n++;

        /* Nothing else runs while this loop does, the main loop included — so the
         * watchdog has to be fed from here or it fires mid-recording and takes the NMI
         * path into the failsafe. Every 256 samples is far inside its ~2 s. */
        if ((n & 0xFF) == 0) {
            wdg_reload(WATCHDOG_DEFAULT_PERIOD);
        }
    }

    mic_off();
    /* Only a capture that stored something replaces the clip. n == 0 — a release
     * landing inside the settle + bias preamble above, before the first nibble — must
     * not wipe a pending, unfetched recording whose bytes this run never touched. */
    if (n != 0) {
        clip_samples = n;
    }
    return n;
}

void mic_fill_ramp(void)
{
    for (uint16_t i = 0; i < CLIP_BYTES; i++) {
        clip[i] = (uint8_t)i;
    }
    clip_samples = CLIP_SAMPLES;
}

void mic_clip_release(void)
{
    clip_samples = 0;
}

const uint8_t *mic_clip(uint16_t *samples)
{
    *samples = clip_samples;
    return clip;
}
