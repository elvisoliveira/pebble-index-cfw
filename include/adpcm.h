#ifndef ADPCM_H_
#define ADPCM_H_

/*
 * IMA ADPCM — 4 bits per sample, about the quality of 12-bit PCM on speech.
 *
 * It is here for range, not for bandwidth: the clip lives in RAM, and RAM is what caps
 * how long a recording can be. At 4 bits a sample instead of 16, the same buffer holds
 * four times the audio. That it also makes the clip cheap enough to stream one day is a
 * second benefit, not the reason.
 *
 * Table-driven and multiply-free, which is what makes it affordable inside the sampling
 * interrupt on a Cortex-M0+.
 */
#include <stdint.h>

typedef struct {
    int16_t predictor;   /* the decoder's running estimate, mirrored here */
    uint8_t index;       /* position in the step table, 0..88 */
} adpcm_state_t;

/* Reset before every clip. A decoder starting from the same zeroed state tracks it. */
void adpcm_reset(adpcm_state_t *st);

/* Encode one signed sample into a nibble, advancing the state. */
uint8_t adpcm_encode(adpcm_state_t *st, int16_t sample);

#endif // ADPCM_H_
