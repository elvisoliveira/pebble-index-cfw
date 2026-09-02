#ifndef MIC_H_
#define MIC_H_

/*
 * Microphone — the ring has an analog MEMS on P0_7, read by the GP_ADC.
 *
 * NOT a PDM part: the DA14535 has no audio peripheral at all (the SDK's PDM driver is
 * `#if !defined (__DA14531__)`, and the GPIO function enum has no PDM or PCM entry).
 * The stock app converts single-ended on channel 3 with the 2x attenuator, chopping on
 * and 64x oversampling — see board_config.h for which of those differ per board.
 *
 * Two things live here: a LEVEL (one 4 ms burst, mic_read) and a RECORDING (a blocking
 * poll loop, mic_capture). Neither is the stock firmware's path — it streams
 * continuously at 10 kHz by putting UART2 in internal loopback and chaining three DMA
 * channels, so the character rate clocks the conversions and no CPU runs between
 * samples. That is still worth porting, and mic_capture says exactly when it becomes
 * necessary: the loop below owns the CPU while it runs, radio included.
 */
#include <stdbool.h>
#include <stdint.h>

/*
 * The clip's sample rate, and the rate the converter actually runs at.
 *
 * The ADC free-runs at whatever its conversion time makes it — measured at about
 * 11 kHz — which is not a rate anyone asked for. 8 kHz is the telephone standard, is
 * plenty for voice, and buys 37% more recording out of the same buffer, so the loop
 * averages the extra samples away rather than keeping them.
 *
 * Averaging, not dropping: discarding samples folds everything above 4 kHz back into
 * the band as aliasing, and a running mean over each output period is a crude low-pass
 * that costs one add. Whoever plays the clip has to use MIC_SAMPLE_RATE_HZ or the pitch
 * comes out wrong.
 */
#define MIC_SAMPLE_RATE_HZ  8000
#define MIC_SOURCE_RATE_HZ  11000

/* One burst, in raw ADC counts. Raw on purpose: the useful range is not known yet on
 * either board, and scaling or thresholding would bake in a guess before the bench has
 * produced one. */
typedef struct {
    uint16_t pp;   /* peak-to-peak: how much the signal moved */
    uint16_t dc;   /* mean: where it sits */
} mic_reading_t;

/*
 * Take a burst and report both numbers.
 *
 * dc is what tells a dead signal path from a live-but-flat one, and they need very
 * different fixes. A MAX9814 rests at 1.23 V typical — 1.14-1.32 V across parts, so a
 * range rather than a number — which under the kit's 3x attenuator is about 29800
 * counts, and ours measured 29835. The ring's MEMS rests at half whatever VBAT_HIGH is.
 *
 * A dc of roughly zero means nothing is arriving at the pin: wiring, power, the wrong
 * pad — or, on a module that is not this one, an output coupling capacitor. The
 * datasheet says a COUT is REQUIRED to strip MICOUT's 1.23 V; ours plainly has none,
 * which is exactly what an ADC wants, since that bias is our midpoint. A board built to
 * the datasheet would hand us a signal centred on nothing, and it would look identical
 * to a dead one here.
 *
 * A plausible dc with a tiny pp means the signal is there and something is flattening
 * it, which on the kit is the AGC's job description (see board_config.h on the AR pin).
 *
 * Blocks for roughly 4 ms. Powers the microphone on the ring for the duration and
 * leaves it off again.
 */
mic_reading_t mic_read(void);

/*
 * Record until told to stop, ADPCM-encoded as it goes.
 *
 * A blocking poll loop, which is the whole simplification: adc_get_sample() already
 * converts and waits, mic_read() already proves it works, and this is that loop run
 * long instead of 64 times. The conversion time sets the rate — deterministic, so it
 * can be measured once and written down.
 *
 * What was tried and does NOT work: the ADC's own continuous mode. Without an interval
 * it does not sustain conversions at all (a five-second hold produced three samples),
 * and the interval register's resolution is 1.024 ms, capping it near 976 Hz. That is
 * why the stock firmware clocks conversions from UART2 in internal loopback through
 * three DMA channels — not cleverness for its own sake, but the only way to get an
 * audio rate out of this ADC. Adopt it when capture has to share the chip with a live
 * radio; this loop cannot, since it blocks everything including BLE.
 *
 * keep() is polled every sample and ends the recording when it returns false.
 */
uint16_t mic_capture(bool (*keep)(void));

/* The clip: one ADPCM nibble per sample, low nibble first. */
const uint8_t *mic_clip(uint16_t *samples);

/*
 * Drop the clip once it has been delivered.
 *
 * This is what makes "samples > 0" mean "there is audio you have not taken" rather than
 * merely "there is audio", and that difference is the whole of the phone's logic: it can
 * fetch on sight instead of guessing whether the clip in the advertisement is one it
 * already has. Comparing sample counts would not do — two recordings of the same length
 * are indistinguishable.
 */
void mic_clip_release(void);

/*
 * Fill the clip with a counting ramp instead of audio.
 *
 * For proving the transfer without depending on the microphone: a dropped, repeated or
 * reordered chunk is obvious in a ramp and invisible in audio, where corrupted noise
 * still looks like noise. Three lines that make the two halves independently testable.
 */
void mic_fill_ramp(void);

#endif // MIC_H_
