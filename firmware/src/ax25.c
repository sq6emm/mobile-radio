#include "ax25.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t ax25_fcs(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return (uint16_t)~crc;
}

bool ax25_addr_from_str(ax25_addr_t *a, const char *s)
{
    memset(a, 0, sizeof(*a));
    if (!s) return false;
    const char *dash = strchr(s, '-');
    size_t n = dash ? (size_t)(dash - s) : strlen(s);
    if (n < 1 || n > 6) return false;
    for (size_t i = 0; i < n; i++) {
        if (!isalnum((unsigned char)s[i])) return false;
        a->call[i] = (char)toupper((unsigned char)s[i]);
    }
    if (dash) {
        if (!isdigit((unsigned char)dash[1])) return false;
        int ssid = atoi(dash + 1);
        if (ssid < 0 || ssid > 15) return false;
        a->ssid = (uint8_t)ssid;
    }
    return true;
}

void ax25_addr_to_str(const ax25_addr_t *a, char *out, size_t cap)
{
    if (a->ssid) snprintf(out, cap, "%s-%u", a->call, a->ssid);
    else snprintf(out, cap, "%s", a->call);
}

static void put_addr(uint8_t *p, const ax25_addr_t *a, bool last, bool cmd_bit)
{
    size_t n = strlen(a->call);
    for (size_t i = 0; i < 6; i++)
        p[i] = (uint8_t)((i < n ? (uint8_t)a->call[i] : ' ') << 1);
    p[6] = (uint8_t)(0x60 | ((a->ssid & 0x0F) << 1) | (last ? 1 : 0) |
                     (cmd_bit ? 0x80 : 0) | (a->repeated ? 0x80 : 0));
}

size_t ax25_build_ui(uint8_t *out, size_t cap, const ax25_addr_t *dst, const ax25_addr_t *src,
                     const ax25_addr_t *digis, int ndigi, const uint8_t *info, size_t info_len)
{
    if (ndigi < 0 || ndigi > AX25_MAX_DIGIS || info_len > AX25_MAX_INFO) return 0;
    size_t len = 14 + (size_t)ndigi * 7 + 2 + info_len + 2;
    if (!out || cap < len) return 0;
    uint8_t *p = out;
    put_addr(p, dst, false, true);  p += 7;           /* command frame: C bit set in dst */
    put_addr(p, src, ndigi == 0, false); p += 7;
    for (int i = 0; i < ndigi; i++) { put_addr(p, &digis[i], i == ndigi - 1, false); p += 7; }
    *p++ = AX25_CTRL_UI;
    *p++ = AX25_PID_NOL3;
    if (info_len) memcpy(p, info, info_len);
    p += info_len;
    uint16_t fcs = ax25_fcs(out, (size_t)(p - out));
    *p++ = (uint8_t)(fcs & 0xFF);
    *p++ = (uint8_t)(fcs >> 8);
    return len;
}

static void get_addr(const uint8_t *p, ax25_addr_t *a)
{
    memset(a, 0, sizeof(*a));
    size_t n = 0;
    for (size_t i = 0; i < 6; i++) {
        char c = (char)(p[i] >> 1);
        if (c != ' ') a->call[n++] = c;
    }
    a->ssid = (p[6] >> 1) & 0x0F;
    a->repeated = (p[6] & 0x80) != 0;
}

bool ax25_parse(const uint8_t *frame, size_t len, ax25_frame_t *out)
{
    if (!frame || !out || len < 18) return false;     /* 2 addr + ctrl + pid + fcs */
    uint16_t want = (uint16_t)(frame[len - 2] | (frame[len - 1] << 8));
    if (ax25_fcs(frame, len - 2) != want) return false;
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    get_addr(frame + pos, &out->dst); pos += 7;
    get_addr(frame + pos, &out->src);
    bool last = frame[pos + 6] & 1; pos += 7;
    while (!last) {
        if (out->ndigi >= AX25_MAX_DIGIS || pos + 7 + 2 + 2 > len) return false;
        get_addr(frame + pos, &out->digi[out->ndigi]);
        last = frame[pos + 6] & 1;
        out->ndigi++;
        pos += 7;
    }
    if (pos + 2 + 2 > len) return false;
    out->ctrl = frame[pos++];
    out->pid = frame[pos++];
    out->info = frame + pos;
    out->info_len = len - 2 - pos;
    return true;
}

/* ---------------------------------------------------------------- HDLC */
static size_t emit(uint8_t *bits, size_t cap, size_t n, int *level, int bit)
{
    if (!bit) *level ^= 1;            /* NRZI: 0 toggles, 1 holds */
    if (n < cap) bits[n] = (uint8_t)*level;
    return n + 1;
}

size_t hdlc_encode(const uint8_t *frame, size_t len, int preamble_flags, int tail_flags,
                   uint8_t *bits, size_t cap, int *level_io)
{
    size_t n = 0;
    int level = level_io ? (*level_io & 1) : 0;
    for (int f = 0; f < preamble_flags; f++)
        for (int b = 0; b < 8; b++) n = emit(bits, cap, n, &level, (0x7E >> b) & 1);
    int ones = 0;
    for (size_t i = 0; i < len; i++) {
        for (int b = 0; b < 8; b++) {
            int bit = (frame[i] >> b) & 1;          /* LSB first */
            n = emit(bits, cap, n, &level, bit);
            if (bit) {
                if (++ones == 5) { n = emit(bits, cap, n, &level, 0); ones = 0; }
            } else ones = 0;
        }
    }
    for (int f = 0; f < tail_flags; f++)
        for (int b = 0; b < 8; b++) n = emit(bits, cap, n, &level, (0x7E >> b) & 1);
    if (level_io) *level_io = level;
    return n <= cap ? n : 0;
}

void hdlc_rx_init(hdlc_rx_t *r, hdlc_frame_cb cb, void *user)
{
    memset(r, 0, sizeof(*r));
    r->cb = cb;
    r->user = user;
}

static void rx_reset_frame(hdlc_rx_t *r)
{
    r->nbytes = 0; r->cur = 0; r->nbits = 0; r->ones = 0;
}

void hdlc_rx_feed(hdlc_rx_t *r, int level)
{
    int bit = (level == r->last_level) ? 1 : 0;   /* NRZI decode */
    r->last_level = level;

    r->shift = ((r->shift << 1) | (uint32_t)bit) & 0xFF;
    if (r->shift == 0x7E) {                        /* flag */
        if (r->in_frame && r->nbits == 7 && r->nbytes >= 17) {
            /* the flag's first 7 bits were counted into cur; the frame ended at the previous byte */
            if (ax25_fcs(r->buf, r->nbytes - 2) ==
                (uint16_t)(r->buf[r->nbytes - 2] | (r->buf[r->nbytes - 1] << 8))) {
                r->frames_ok++;
                if (r->cb) r->cb(r->user, r->buf, r->nbytes);
            } else {
                r->fcs_errors++;
            }
        }
        r->in_frame = true;
        rx_reset_frame(r);
        return;
    }
    if (!r->in_frame) return;

    if (bit) {
        if (++r->ones > 6) { r->in_frame = false; return; }   /* abort / idle */
    } else {
        if (r->ones == 5) { r->ones = 0; return; }           /* stuffed zero: drop */
        r->ones = 0;
    }
    r->cur |= (uint8_t)(bit << r->nbits);
    if (++r->nbits == 8) {
        if (r->nbytes < sizeof r->buf) r->buf[r->nbytes++] = r->cur;
        else r->in_frame = false;
        r->cur = 0; r->nbits = 0;
    }
}
