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
 * One thing lives here: a RECORDING (a blocking poll loop, mic_capture). A per-click
 * LEVEL read used to sit beside it and was removed, because it ran in the button ISR and
 * the SDK polls this same GP_ADC from task context for its radio temperature calibration
 * (conditionally_run_radio_cals, every 2 s while awake): an ISR reconfiguring the
 * converter under that poll hands it a microphone sample as a temperature, or leaves it
 * spinning on a converter the ISR just disabled. mic_capture runs in task context, the
 * same context as that poll, so the two cannot overlap.
 *
 * This is not the stock firmware's path — it streams continuously at 10 kHz by putting
 * UART2 in internal loopback and chaining three DMA channels, so the character rate
 * clocks the conversions and no CPU runs between samples. That is still worth porting,
 * and mic_capture says exactly when it becomes necessary: the loop below owns the CPU
 * while it runs.
 */
#include <stdbool.h>
#include <stdint.h>

/*
 * The clip's sample rate, and the rate the converter actually runs at.
 *
 * The ADC free-runs at whatever its conversion time makes it, which is not a rate
 * anyone asked for. 8 kHz is the telephone standard, is plenty for voice, and buys 77%
 * more recording out of the same buffer, so the loop averages the extra samples away
 * rather than keeping them.
 *
 * Averaging, not dropping: discarding samples folds everything above 4 kHz back into
 * the band as aliasing, and a running mean over each output period is a crude low-pass
 * that costs one add. Whoever plays the clip has to use MIC_SAMPLE_RATE_HZ or the pitch
 * comes out wrong.
 *
 * MIC_SOURCE_RATE_HZ is a MEASURED per-board number and lives in board_config.h with
 * the other things a board decides. Getting it wrong does not fail loudly: the clip is
 * still labelled 8 kHz, so it simply plays at the wrong speed, and a voice 20% slow
 * reads as "the equalisation is off" long before it reads as "the sample rate is off".
 */
#define MIC_SAMPLE_RATE_HZ  8000

/*
 * Record until told to stop, ADPCM-encoded as it goes.
 *
 * A blocking poll loop, which is the whole simplification: adc_get_sample() already
 * converts and waits, and this is that call run in a loop. The conversion time sets the
 * rate — deterministic, so it can be measured once and written down. The caller keeps
 * the radio off for the duration (user_app.c stops a running burst first), because the
 * BLE interrupts preempting this loop would stretch that clock and warp the pitch.
 *
 * What was tried and does NOT work: the ADC's own continuous mode. Without an interval
 * it does not sustain conversions at all (a five-second hold produced three samples),
 * and the interval register's resolution is 1.024 ms, capping it near 976 Hz. That is
 * why the stock firmware clocks conversions from UART2 in internal loopback through
 * three DMA channels — not cleverness for its own sake, but the only way to get an
 * audio rate out of this ADC. Adopt it when capture has to share the chip with a live
 * radio; this loop cannot, since it blocks the host for as long as it runs.
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
