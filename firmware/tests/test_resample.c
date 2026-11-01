#include "resample.h"
#include "test.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double rms(const int16_t *x, size_t n, size_t skip)
{
    double s = 0; size_t c = 0;
    for (size_t i = skip; i < n; i++) { s += (double)x[i] * x[i]; c++; }
    return sqrt(s / (double)c);
}

static void tone(int16_t *x, size_t n, double f, double fs, double amp)
{
    for (size_t i = 0; i < n; i++) x[i] = (int16_t)lrint(amp * sin(2 * M_PI * f * i / fs));
}

int main(void)
{
    static int16_t a[4800], b[14400], c[4800];
    resample_t up, down;

    /* 1 kHz tone: 16k -> 48k -> 16k keeps level within 0.3 dB */
    tone(a, 4800, 1000.0, 16000.0, 10000.0);
    resample_init(&up); resample_init(&down);
    CHECK_EQ(resample_16_to_48(&up, a, 4800, b), 14400);
    CHECK_EQ(resample_48_to_16(&down, b, 14400, c), 4800);
    double r_in = rms(a, 4800, 200), r_out = rms(c, 4800, 200);
    double loss_db = 20 * log10(r_out / r_in);
    printf("1 kHz round trip: %.2f dB\n", loss_db);
    CHECK(fabs(loss_db) < 0.3);

    /* 3 kHz passband */
    tone(a, 4800, 3000.0, 16000.0, 10000.0);
    resample_init(&up); resample_init(&down);
    resample_16_to_48(&up, a, 4800, b); resample_48_to_16(&down, b, 14400, c);
    loss_db = 20 * log10(rms(c, 4800, 200) / rms(a, 4800, 200));
    printf("3 kHz round trip: %.2f dB\n", loss_db);
    CHECK(fabs(loss_db) < 1.0);

    /* 10 kHz at 48 kHz would alias to 6 kHz: must be suppressed > 50 dB */
    tone(b, 14400, 10000.0, 48000.0, 10000.0);
    resample_init(&down);
    resample_48_to_16(&down, b, 14400, c);
    double att = 20 * log10(rms(c, 4800, 200) / 10000.0 * sqrt(2.0));
    printf("10 kHz alias: %.1f dB\n", att);
    CHECK(att < -50.0);

    /* interpolation images at 16 kHz +/- f are suppressed: 1 kHz up-sampled, check 15/17 kHz by power ratio */
    tone(a, 4800, 1000.0, 16000.0, 10000.0);
    resample_init(&up);
    resample_16_to_48(&up, a, 4800, b);
    /* crude: total power should equal the 1 kHz tone power (images would add power) */
    double p = rms(b, 14400, 600) / (10000.0 / sqrt(2.0));
    printf("up-sampled level ratio: %.3f\n", p);
    CHECK(fabs(p - 1.0) < 0.05);

    /* streaming equivalence: chunked == whole */
    static int16_t c2[4800];
    resample_init(&down);
    size_t n = 0;
    for (size_t off = 0; off < 14400; off += 7) {
        size_t len = (14400 - off < 7) ? 14400 - off : 7;
        n += resample_48_to_16(&down, b + off, len, c2 + n);
    }
    resample_init(&down);
    resample_48_to_16(&down, b, 14400, c);
    CHECK_EQ(n, 4800);
    CHECK(memcmp(c, c2, sizeof c) == 0);
    TEST_END();
}
