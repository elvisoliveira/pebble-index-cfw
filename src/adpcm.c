/* IMA ADPCM encoder — see include/adpcm.h. Tables are the standard ones. */
#include <adpcm.h>

static const int16_t step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static const int8_t index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

void adpcm_reset(adpcm_state_t *st)
{
    st->predictor = 0;
    st->index = 0;
}

uint8_t adpcm_encode(adpcm_state_t *st, int16_t sample)
{
    int32_t step = step_table[st->index];
    int32_t diff = (int32_t)sample - st->predictor;
    uint8_t code = 0;

    if (diff < 0) {
        code = 8;               /* sign bit */
        diff = -diff;
    }

    /* Three magnitude bits, each worth half the previous — the same ladder the decoder
     * walks back up, which is why the two stay in step without sending the predictor. */
    int32_t delta = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 1;               delta += step; }

    int32_t predicted = (code & 8) ? st->predictor - delta : st->predictor + delta;
    /* Clamp: the predictor is 16-bit and a loud passage can push it past the rails.
     * Letting it wrap would invert the waveform, which sounds like a crack. */
    if (predicted > 32767)  predicted = 32767;
    if (predicted < -32768) predicted = -32768;
    st->predictor = (int16_t)predicted;

    int8_t index = (int8_t)st->index + index_table[code];
    if (index < 0)  index = 0;
    if (index > 88) index = 88;
    st->index = (uint8_t)index;

    return code;
}
