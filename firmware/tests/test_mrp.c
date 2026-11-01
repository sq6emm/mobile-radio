#include "mrp.h"
#include "test.h"

static int n_frames;
static uint8_t last_type, last_seq, last_payload[MRP_MAX_PAYLOAD];
static uint16_t last_len;

static void cb(void *u, uint8_t type, uint8_t seq, const uint8_t *pl, uint16_t len)
{
    (void)u;
    n_frames++;
    last_type = type; last_seq = seq; last_len = len;
    memcpy(last_payload, pl, len);
}

int main(void)
{
    /* CRC-16/CCITT-FALSE check value */
    CHECK_EQ(mrp_crc16((const uint8_t *)"123456789", 9, MRP_CRC_INIT), 0x29B1);

    /* known frame: PTT key, seq 1, ttl 0  -> shared vector with tools/mrp.py */
    uint8_t f[MRP_MAX_FRAME];
    mrp_ptt_t p = { 1, 0 };
    uint8_t pl[MRP_PTT_LEN];
    size_t n = mrp_encode(f, sizeof f, MRP_PTT, 1, pl, (uint16_t)mrp_pack_ptt(&p, pl));
    CHECK_EQ(n, MRP_HDR_LEN + MRP_PTT_LEN + MRP_CRC_LEN);
    static const uint8_t expect[] = { 0xA5, 0x03, 0x01, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00, 0x4E, 0xCE };
    CHECK(memcmp(f, expect, sizeof expect) == 0);

    /* round trip through the parser, byte by byte */
    mrp_parser_t ps;
    mrp_parser_init(&ps, cb, NULL);
    for (size_t i = 0; i < n; i++) mrp_parser_feed(&ps, f + i, 1);
    CHECK_EQ(n_frames, 1);
    CHECK_EQ(last_type, MRP_PTT);
    CHECK_EQ(last_seq, 1);
    CHECK_EQ(last_len, MRP_PTT_LEN);
    mrp_ptt_t q;
    CHECK(mrp_unpack_ptt(last_payload, last_len, &q));
    CHECK_EQ(q.state, 1);

    /* garbage, a corrupted frame, then two good frames back to back in one chunk */
    uint8_t stream[600]; size_t s = 0;
    const uint8_t junk[] = { 0x00, 0xA5, 0xFF, 0x12, 0xA5 };
    memcpy(stream + s, junk, sizeof junk); s += sizeof junk;
    memcpy(stream + s, f, n); stream[s + 7] ^= 0x55; s += n;       /* corrupt payload -> CRC error */
    memcpy(stream + s, f, n); s += n;
    uint8_t big[MRP_AUDIO_FRAME_BYTES];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 7);
    uint8_t f2[MRP_MAX_FRAME];
    size_t n2 = mrp_encode(f2, sizeof f2, MRP_AUDIO_TX, 9, big, sizeof big);
    CHECK_EQ(n2, MRP_HDR_LEN + sizeof big + MRP_CRC_LEN);
    /* feed the big frame split across two arbitrary chunks */
    n_frames = 0;
    mrp_parser_feed(&ps, stream, s);
    CHECK_EQ(n_frames, 1);
    CHECK_EQ(ps.crc_errors, 1);
    CHECK(ps.resyncs >= 1);
    mrp_parser_feed(&ps, f2, 100);
    CHECK_EQ(n_frames, 1);
    mrp_parser_feed(&ps, f2 + 100, n2 - 100);
    CHECK_EQ(n_frames, 2);
    CHECK_EQ(last_type, MRP_AUDIO_TX);
    CHECK_EQ(last_len, sizeof big);
    CHECK(memcmp(last_payload, big, sizeof big) == 0);

    /* oversize length field (with a valid header check) must be rejected and resynchronised */
    uint8_t bad[] = { 0xA5, 0x10, 0x00, 0xFF, 0xFF, 0x10 };
    n_frames = 0;
    mrp_parser_feed(&ps, bad, sizeof bad);
    mrp_parser_feed(&ps, f, n);
    CHECK_EQ(n_frames, 1);

    /* encoder limits */
    CHECK_EQ(mrp_encode(f, 5, MRP_PTT, 0, pl, 3), 0);
    CHECK_EQ(mrp_encode(f, sizeof f, MRP_PTT, 0, NULL, 3), 0);
    CHECK_EQ(mrp_encode(f, sizeof f, MRP_GET_INFO, 0, NULL, 0), MRP_HDR_LEN + MRP_CRC_LEN);

    /* status pack/unpack with negative fields */
    mrp_status_t st = { .flags = MRP_ST_PTT | MRP_ST_SOURCING, .rssi_dbm = -101, .channel = 16,
                        .batt_mv = 7612, .batt_ma = -850, .soc_pct = 77, .temp_c = -5,
                        .vbus_mv = 9000, .vbus_ma = 1500, .pd_role = MRP_PD_SOURCE,
                        .pd_mv = 9000, .pd_ma = 2000, .tx_remaining_s = 179 };
    uint8_t sb[MRP_STATUS_LEN];
    CHECK_EQ(mrp_pack_status(&st, sb), MRP_STATUS_LEN);
    mrp_status_t st2;
    CHECK(mrp_unpack_status(sb, MRP_STATUS_LEN, &st2));
    CHECK(!mrp_unpack_status(sb, MRP_STATUS_LEN - 1, &st2));
    CHECK_EQ(st2.rssi_dbm, -101); CHECK_EQ(st2.batt_ma, -850); CHECK_EQ(st2.temp_c, -5);
    CHECK_EQ(st2.vbus_ma, 1500); CHECK_EQ(st2.tx_remaining_s, 179); CHECK_EQ(st2.flags, st.flags);

    mrp_info_t in = { .proto = 1, .fw_major = 0, .fw_minor = 1, .hw_rev = 1, .caps = 0xDEADBEEF,
                      .serial = "MR-0001", .region = 0, .n_channels = 16 };
    uint8_t ib[MRP_INFO_LEN]; mrp_info_t in2;
    CHECK_EQ(mrp_pack_info(&in, ib), MRP_INFO_LEN);
    CHECK(mrp_unpack_info(ib, MRP_INFO_LEN, &in2));
    CHECK_EQ(in2.caps, 0xDEADBEEF); CHECK_STR(in2.serial, "MR-0001"); CHECK_EQ(in2.n_channels, 16);

    mrp_set_channel_t c = { 7, 0, 12, 12, 4, 1, MRP_CHAN_FLAG_BCLO }, c2;
    uint8_t cb_[MRP_SET_CHANNEL_LEN];
    CHECK_EQ(mrp_pack_set_channel(&c, cb_), MRP_SET_CHANNEL_LEN);
    CHECK(mrp_unpack_set_channel(cb_, MRP_SET_CHANNEL_LEN, &c2));
    CHECK_EQ(c2.channel, 7); CHECK_EQ(c2.flags, MRP_CHAN_FLAG_BCLO);
    TEST_END();
}
