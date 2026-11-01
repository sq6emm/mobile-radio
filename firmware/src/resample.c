#include "resample.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

void resample_init(resample_t *r)
{
    memset(r, 0, sizeof(*r));
    /* windowed-sinc low-pass, cutoff 6.0 kHz at 48 kHz (fc = 0.125), Blackman window */
    const double fc = 6000.0 / 48000.0;
    double sum = 0.0;
    for (int n = 0; n < RS_TAPS; n++) {
        double m = n - (RS_TAPS - 1) / 2.0;
        double sinc = (m == 0.0) ? 2.0 * fc : sin(2.0 * M_PI * fc * m) / (M_PI * m);
        double w = 0.42 - 0.5 * cos(2.0 * M_PI * n / (RS_TAPS - 1)) + 0.08 * cos(4.0 * M_PI * n / (RS_TAPS - 1));
        r->h[n] = (float)(sinc * w);
        sum += r->h[n];
    }
    for (int n = 0; n < RS_TAPS; n++) r->h[n] = (float)(r->h[n] / sum);
}

static inline void push(resample_t *r, float v)
{
    r->hist[r->pos] = v;
    r->pos = (r->pos + 1) % RS_TAPS;
}

static inline float fir(const resample_t *r)
{
    float acc = 0.0f;
    int idx = r->pos;
    for (int k = 0; k < RS_TAPS; k++) {
        idx = (idx == 0) ? RS_TAPS - 1 : idx - 1;
        acc += r->hist[idx] * r->h[k];
    }
    return acc;
}

static inline int16_t sat(float v)
{
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)lrintf(v);
}

size_t resample_48_to_16(resample_t *r, const int16_t *in, size_t n_in, int16_t *out)
{
    size_t n_out = 0;
    for (size_t i = 0; i < n_in; i++) {
        push(r, (float)in[i]);
        if (++r->phase == 3) {
            r->phase = 0;
            out[n_out++] = sat(fir(r));
        }
    }
    return n_out;
}

size_t resample_16_to_48(resample_t *r, const int16_t *in, size_t n_in, int16_t *out)
{
    size_t n_out = 0;
    for (size_t i = 0; i < n_in; i++) {
        push(r, 3.0f * (float)in[i]);       /* zero-stuffing with gain 3 */
        out[n_out++] = sat(fir(r));
        push(r, 0.0f);
        out[n_out++] = sat(fir(r));
        push(r, 0.0f);
        out[n_out++] = sat(fir(r));
    }
    return n_out;
}
