#include "afsk.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

#define SINE_N 1024
static int16_t sine_tab[SINE_N];
static int sine_ready;

static void sine_init(void)
{
    if (sine_ready) return;
    for (int i = 0; i < SINE_N; i++)
        sine_tab[i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * i / SINE_N));
    sine_ready = 1;
}

void afsk_mod_init(afsk_mod_t *m, uint32_t fs, int16_t amplitude)
{
    sine_init();
    memset(m, 0, sizeof(*m));
    m->fs = fs;
    m->amplitude = amplitude;
    m->mark_inc = (uint32_t)((double)AFSK_MARK_HZ / fs * 4294967296.0);
    m->space_inc = (uint32_t)((double)AFSK_SPACE_HZ / fs * 4294967296.0);
}

size_t afsk_mod_max_samples(const afsk_mod_t *m, size_t nbits)
{
    return (nbits * m->fs) / AFSK_BAUD + nbits + 1;
}

size_t afsk_mod_run(afsk_mod_t *m, const uint8_t *levels, size_t nbits, int16_t *pcm, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < nbits; i++) {
        m->frac += m->fs;
        uint32_t ns = m->frac / AFSK_BAUD;      /* 13 or 14 samples at 16 kHz, exact on average */
        m->frac -= ns * AFSK_BAUD;
        uint32_t inc = levels[i] ? m->mark_inc : m->space_inc;
        if (n + ns > cap) return 0;
        for (uint32_t k = 0; k < ns; k++) {
            m->phase += inc;
            pcm[n++] = (int16_t)(((int32_t)sine_tab[m->phase >> 22] * m->amplitude) >> 15);
        }
    }
    return n;
}

/* ------------------------------------------------------------- demod */
void afsk_demod_init(afsk_demod_t *d, uint32_t fs, hdlc_rx_t *out)
{
    memset(d, 0, sizeof(*d));
    d->fs = fs;
    d->out = out;
    d->win = (int)((fs + AFSK_BAUD / 2) / AFSK_BAUD);
    if (d->win > AFSK_MAX_WIN) d->win = AFSK_MAX_WIN;
    for (int k = 0; k < d->win; k++) {
        double tm = 2.0 * M_PI * AFSK_MARK_HZ * k / fs, ts = 2.0 * M_PI * AFSK_SPACE_HZ * k / fs;
        d->cm[k] = (float)cos(tm); d->sm[k] = (float)sin(tm);
        d->cs[k] = (float)cos(ts); d->ss[k] = (float)sin(ts);
    }
    /* RBJ band-pass, centre ~1600 Hz, Q 0.9 (roughly 900-2700 Hz) */
    double f0 = 1600.0, Q = 0.9;
    double w0 = 2.0 * M_PI * f0 / fs, alpha = sin(w0) / (2.0 * Q), a0 = 1.0 + alpha;
    d->b0 = (float)(alpha / a0); d->b1 = 0.0f; d->b2 = (float)(-alpha / a0);
    d->a1 = (float)(-2.0 * cos(w0) / a0); d->a2 = (float)((1.0 - alpha) / a0);
    d->pll_inc = (float)AFSK_BAUD / (float)fs;
    d->pll_gain = 0.3f;
    d->pll = 0.0f;
    d->last_sign = 1;
}

void afsk_demod_process(afsk_demod_t *d, const int16_t *pcm, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        /* band-pass (transposed direct form II) */
        float in = (float)pcm[i] * (1.0f / 32768.0f);
        float y = d->b0 * in + d->z1;
        d->z1 = d->b1 * in - d->a1 * y + d->z2;
        d->z2 = d->b2 * in - d->a2 * y;

        d->x[d->xpos] = y;
        d->xpos = (d->xpos + 1) % d->win;

        /* correlate over the last 'win' samples; age k = 0 is the newest */
        float im = 0, qm = 0, is = 0, qs = 0;
        int idx = d->xpos;
        for (int k = 0; k < d->win; k++) {
            idx = (idx == 0) ? d->win - 1 : idx - 1;
            float v = d->x[idx];
            im += v * d->cm[k]; qm += v * d->sm[k];
            is += v * d->cs[k]; qs += v * d->ss[k];
        }
        float em = im * im + qm * qm, es = is * is + qs * qs;
        float soft = (em - es) / (em + es + 1e-9f);
        d->lp += 0.6f * (soft - d->lp);
        int sign = d->lp > 0.0f ? 1 : 0;

        /* bit clock: sample when the phase wraps; pull transitions to phase 0.5 */
        if (sign != d->last_sign) {
            d->pll += (0.5f - d->pll) * d->pll_gain;
            d->last_sign = sign;
        }
        d->pll += d->pll_inc;
        if (d->pll >= 1.0f) {
            d->pll -= 1.0f;
            d->bits_out++;
            if (d->out) hdlc_rx_feed(d->out, sign);
        }
    }
}
