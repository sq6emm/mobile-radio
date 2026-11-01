/*
 * Bell 202 AFSK modem (1200 Bd, mark 1200 Hz, space 2200 Hz) for AX.25.
 *
 * Modulator: continuous-phase NCO; input is the NRZI line level stream from
 * hdlc_encode(); output is s16 PCM at the configured sample rate.
 *
 * Demodulator: 2nd-order band-pass pre-filter, one-bit quadrature correlators
 * at both tones, normalised energy difference, digital PLL bit sync; sampled
 * levels are fed to an hdlc_rx_t which does NRZI decoding and framing.
 */
#ifndef AFSK_H
#define AFSK_H

#include <stddef.h>
#include <stdint.h>
#include "ax25.h"

#define AFSK_BAUD      1200
#define AFSK_MARK_HZ   1200
#define AFSK_SPACE_HZ  2200
#define AFSK_MAX_WIN   48      /* samples per bit at up to 48 kHz */

typedef struct {
    uint32_t fs;
    uint32_t phase, mark_inc, space_inc;
    uint32_t frac;             /* fractional samples-per-bit accumulator */
    int16_t  amplitude;
} afsk_mod_t;

void   afsk_mod_init(afsk_mod_t *m, uint32_t fs, int16_t amplitude);
/* Upper bound of samples produced for nbits. */
size_t afsk_mod_max_samples(const afsk_mod_t *m, size_t nbits);
/* Modulate 'levels' (0/1 per byte). Returns samples written (0 if cap too small). */
size_t afsk_mod_run(afsk_mod_t *m, const uint8_t *levels, size_t nbits, int16_t *pcm, size_t cap);

typedef struct {
    uint32_t fs;
    int   win;
    float cm[AFSK_MAX_WIN], sm[AFSK_MAX_WIN], cs[AFSK_MAX_WIN], ss[AFSK_MAX_WIN];
    float x[AFSK_MAX_WIN];
    int   xpos;
    /* band-pass biquad */
    float b0, b1, b2, a1, a2, z1, z2;
    float lp;                  /* smoothed decision */
    float pll, pll_inc, pll_gain;
    int   last_sign;
    hdlc_rx_t *out;
    uint32_t bits_out;
} afsk_demod_t;

void afsk_demod_init(afsk_demod_t *d, uint32_t fs, hdlc_rx_t *out);
void afsk_demod_process(afsk_demod_t *d, const int16_t *pcm, size_t n);

#endif
