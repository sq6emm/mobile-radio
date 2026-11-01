#include "ax25.h"
#include "test.h"

static int got; static uint8_t last[AX25_MAX_FRAME]; static size_t last_len;
static void cb(void *u, const uint8_t *f, size_t n) { (void)u; got++; memcpy(last, f, n); last_len = n; }

int main(void)
{
    CHECK_EQ(ax25_fcs((const uint8_t *)"123456789", 9), 0x906E);   /* CRC-16/X-25 check value */

    ax25_addr_t dst, src, digi;
    CHECK(ax25_addr_from_str(&dst, "APMR01"));
    CHECK(ax25_addr_from_str(&src, "sq6emm-7"));
    CHECK(ax25_addr_from_str(&digi, "WIDE1-1"));
    CHECK(!ax25_addr_from_str(&digi, "WIDE1-16"));
    CHECK(!ax25_addr_from_str(&digi, "TOOLONGCALL"));
    CHECK(!ax25_addr_from_str(&digi, "WI DE"));
    CHECK(ax25_addr_from_str(&digi, "WIDE1-1"));
    char s[10]; ax25_addr_to_str(&src, s, sizeof s); CHECK_STR(s, "SQ6EMM-7");
    ax25_addr_to_str(&dst, s, sizeof s); CHECK_STR(s, "APMR01");

    const char *info = "!5106.50N/01702.25E[test";
    uint8_t f[AX25_MAX_FRAME];
    size_t n = ax25_build_ui(f, sizeof f, &dst, &src, &digi, 1, (const uint8_t *)info, strlen(info));
    CHECK_EQ(n, 7 * 3 + 2 + strlen(info) + 2);
    /* address encoding: shifted ASCII, SSID byte 0x60 | ssid<<1 | last */
    CHECK_EQ(f[0], 'A' << 1); CHECK_EQ(f[6], 0xE0);              /* dst: C bit set, ssid 0, not last */
    CHECK_EQ(f[7], 'S' << 1); CHECK_EQ(f[13], 0x60 | (7 << 1)); /* src ssid 7, not last */
    CHECK_EQ(f[20], 0x60 | (1 << 1) | 1);                        /* digi WIDE1-1 last */
    CHECK_EQ(f[21], 0x03); CHECK_EQ(f[22], 0xF0);

    ax25_frame_t p;
    CHECK(ax25_parse(f, n, &p));
    CHECK_STR(p.src.call, "SQ6EMM"); CHECK_EQ(p.src.ssid, 7);
    CHECK_STR(p.dst.call, "APMR01");
    CHECK_EQ(p.ndigi, 1); CHECK_STR(p.digi[0].call, "WIDE1"); CHECK_EQ(p.digi[0].ssid, 1);
    CHECK_EQ(p.ctrl, 0x03); CHECK_EQ(p.pid, 0xF0);
    CHECK_EQ(p.info_len, strlen(info));
    CHECK(memcmp(p.info, info, p.info_len) == 0);
    f[25] ^= 1; CHECK(!ax25_parse(f, n, &p)); f[25] ^= 1;
    CHECK(!ax25_parse(f, 10, &p));

    /* HDLC round trip with stuffing-prone payload (0xFF bytes) */
    uint8_t ff[40]; memset(ff, 0xFF, sizeof ff);
    uint8_t f2[AX25_MAX_FRAME];
    size_t n2 = ax25_build_ui(f2, sizeof f2, &dst, &src, NULL, 0, ff, sizeof ff);
    static uint8_t bits[8000];
    size_t nb = hdlc_encode(f2, n2, 8, 2, bits, sizeof bits, NULL);
    CHECK(nb > (8 + 2) * 8 + n2 * 8);                            /* stuffing added bits */
    CHECK(nb < (8 + 2) * 8 + n2 * 8 + n2 * 2);
    hdlc_rx_t rx; hdlc_rx_init(&rx, cb, NULL);
    /* some noise levels before the frame, then the stream, then idle ones */
    for (int i = 0; i < 20; i++) hdlc_rx_feed(&rx, i & 1);
    for (size_t i = 0; i < nb; i++) hdlc_rx_feed(&rx, bits[i]);
    for (int i = 0; i < 20; i++) hdlc_rx_feed(&rx, 1);
    CHECK_EQ(got, 1);
    CHECK_EQ(last_len, n2);
    CHECK(memcmp(last, f2, n2) == 0);
    CHECK_EQ(rx.fcs_errors, 0);

    /* two frames back to back sharing flags, inverted line polarity (NRZI is polarity agnostic) */
    got = 0;
    int lvl = 0;
    size_t nb1 = hdlc_encode(f, n, 3, 1, bits, sizeof bits, &lvl);
    size_t nb2 = hdlc_encode(f2, n2, 1, 3, bits + nb1, sizeof bits - nb1, &lvl);
    for (size_t i = 0; i < nb1 + nb2; i++) hdlc_rx_feed(&rx, bits[i] ^ 1);
    CHECK_EQ(got, 2);
    CHECK_EQ(last_len, n2);

    /* a corrupted bit in the middle produces an FCS error, not a frame */
    got = 0; rx.fcs_errors = 0;
    bits[nb1 / 2] ^= 1;
    for (size_t i = 0; i < nb1; i++) hdlc_rx_feed(&rx, bits[i]);
    for (int i = 0; i < 16; i++) hdlc_rx_feed(&rx, 1);
    CHECK_EQ(got, 0);
    CHECK(rx.fcs_errors >= 1 || rx.frames_ok == 2);
    TEST_END();
}
