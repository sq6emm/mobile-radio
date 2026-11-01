#include "afsk.h"
#include "ax25.h"
#include "test.h"
#include <math.h>

/* deterministic Gaussian noise */
static uint32_t rng = 12345;
static float urand(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 16777216.0f; }
static float gauss(void) { float u1 = urand() + 1e-9f, u2 = urand(); return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2); }

static int got; static uint8_t last[AX25_MAX_FRAME]; static size_t last_len;
static void cb(void *u, const uint8_t *f, size_t n) { (void)u; got++; memcpy(last, f, n); last_len = n; }

static size_t make_frame(uint8_t *f, size_t cap, int seed)
{
    ax25_addr_t dst, src, digi;
    ax25_addr_from_str(&dst, "APMR01"); ax25_addr_from_str(&src, "SQ6EMM-7"); ax25_addr_from_str(&digi, "WIDE1-1");
    char info[80];
    int n = snprintf(info, sizeof info, "!5106.50N/01702.25E[pkt %d test of the modem", seed);
    return ax25_build_ui(f, cap, &dst, &src, &digi, 1, (const uint8_t *)info, (size_t)n);
}

/* Run 'count' packets through mod -> channel -> demod. Returns packets decoded correctly. */
static int run(int count, uint32_t fs_mod, uint32_t fs_demod, float snr_db, float gain)
{
    static uint8_t bits[20000]; static int16_t pcm[200000]; static uint8_t frame[AX25_MAX_FRAME];
    int ok = 0;
    for (int p = 0; p < count; p++) {
        size_t fl = make_frame(frame, sizeof frame, p);
        size_t nb = hdlc_encode(frame, fl, 20, 3, bits, sizeof bits, NULL);
        afsk_mod_t m; afsk_mod_init(&m, fs_mod, 8000);
        size_t ns = afsk_mod_run(&m, bits, nb, pcm, sizeof pcm / sizeof pcm[0]);
        if (!ns) return -1;
        float sig_p = 8000.0f * 8000.0f / 2.0f;
        float sigma = (snr_db > 90.0f) ? 0.0f : sqrtf(sig_p / powf(10.0f, snr_db / 10.0f));
        for (size_t i = 0; i < ns; i++) {
            float v = gain * pcm[i] + sigma * gauss();
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            pcm[i] = (int16_t)v;
        }
        hdlc_rx_t rx; hdlc_rx_init(&rx, cb, NULL);
        afsk_demod_t d; afsk_demod_init(&d, fs_demod, &rx);
        got = 0;
        /* some silence (noise only) before and after */
        static int16_t pad[4000];
        for (int i = 0; i < 4000; i++) pad[i] = (int16_t)(sigma * gauss());
        afsk_demod_process(&d, pad, 4000);
        afsk_demod_process(&d, pcm, ns);
        afsk_demod_process(&d, pad, 4000);
        if (got >= 1 && last_len == fl && memcmp(last, frame, fl) == 0) ok++;
    }
    return ok;
}

int main(void)
{
    /* modulator sample budget is exact on average: 1200 bits at 16 kHz = 16000 samples */
    afsk_mod_t m; afsk_mod_init(&m, 16000, 8000);
    static uint8_t ones[1200]; memset(ones, 1, sizeof ones);
    static int16_t buf[20000];
    CHECK_EQ(afsk_mod_run(&m, ones, 1200, buf, 20000), 16000);
    CHECK(afsk_mod_max_samples(&m, 1200) >= 16000);
    CHECK_EQ(afsk_mod_run(&m, ones, 1200, buf, 100), 0);     /* cap too small */

    /* clean channel, 16 kHz */
    CHECK_EQ(run(20, 16000, 16000, 100.0f, 1.0f), 20);
    /* clean channel, 48 kHz */
    CHECK_EQ(run(10, 48000, 48000, 100.0f, 1.0f), 10);
    /* weak and loud signals (level independence) */
    CHECK_EQ(run(10, 16000, 16000, 100.0f, 0.05f), 10);
    CHECK_EQ(run(10, 16000, 16000, 100.0f, 3.5f), 10);
    /* +/-2 % sample-rate (baud) offset between the stations */
    CHECK_EQ(run(10, 16320, 16000, 100.0f, 1.0f), 10);
    CHECK_EQ(run(10, 15680, 16000, 100.0f, 1.0f), 10);
    /* noise: full-band SNR (16 kHz) */
    int ok20 = run(50, 16000, 16000, 20.0f, 1.0f);
    int ok10 = run(50, 16000, 16000, 10.0f, 1.0f);
    int ok6 = run(50, 16000, 16000, 6.0f, 1.0f);
    printf("AFSK packet success: 20 dB %d/50, 10 dB %d/50, 6 dB %d/50\n", ok20, ok10, ok6);
    CHECK(ok20 >= 50);
    CHECK(ok10 >= 48);
    CHECK(ok6 >= 25);
    TEST_END();
}
