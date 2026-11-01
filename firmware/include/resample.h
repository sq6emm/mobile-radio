/* 48 kHz <-> 16 kHz FIR resampling (factor 3), streaming, mono s16. */
#ifndef RESAMPLE_H
#define RESAMPLE_H

#include <stddef.h>
#include <stdint.h>

#define RS_TAPS 96      /* multiple of 3 */

typedef struct {
    float h[RS_TAPS];
    float hist[RS_TAPS];
    int   pos;
    int   phase;        /* decimation phase 0..2 */
} resample_t;

void   resample_init(resample_t *r);
/* Decimate: returns number of output samples (about n_in / 3). out needs n_in/3 + 1. */
size_t resample_48_to_16(resample_t *r, const int16_t *in, size_t n_in, int16_t *out);
/* Interpolate: returns 3 * n_in samples. out needs 3 * n_in. */
size_t resample_16_to_48(resample_t *r, const int16_t *in, size_t n_in, int16_t *out);

#endif
