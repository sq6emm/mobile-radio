#include "mrp.h"
#include <string.h>

/* ------------------------------------------------------------------ CRC */
uint16_t mrp_crc16(const uint8_t *data, size_t len, uint16_t crc)
{
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* ------------------------------------------------------------- encoder */
size_t mrp_encode(uint8_t *out, size_t cap, uint8_t type, uint8_t seq,
                  const void *payload, uint16_t len)
{
    if (len > MRP_MAX_PAYLOAD || (len && !payload))
        return 0;
    size_t total = MRP_HDR_LEN + len + MRP_CRC_LEN;
    if (!out || cap < total)
        return 0;
    out[0] = MRP_SOF;
    out[1] = type;
    out[2] = seq;
    out[3] = (uint8_t)(len & 0xFF);
    out[4] = (uint8_t)(len >> 8);
    out[5] = (uint8_t)(out[1] ^ out[2] ^ out[3] ^ out[4]);
    if (len)
        memcpy(out + MRP_HDR_LEN, payload, len);
    uint16_t crc = mrp_crc16(out + 1, (size_t)MRP_HDR_LEN - 1 + len, MRP_CRC_INIT);
    out[total - 2] = (uint8_t)(crc & 0xFF);
    out[total - 1] = (uint8_t)(crc >> 8);
    return total;
}

/* -------------------------------------------------------------- parser */
void mrp_parser_init(mrp_parser_t *p, mrp_frame_cb cb, void *user)
{
    memset(p, 0, sizeof(*p));
    p->cb = cb;
    p->user = user;
}

/* Examine buf[0..pos). 1 = frame delivered, 0 = need more bytes, -1 = junk at buf[0]. */
static int parser_try(mrp_parser_t *p)
{
    if (p->buf[0] != MRP_SOF)
        return -1;
    if (p->pos < MRP_HDR_LEN)
        return 0;
    if ((uint8_t)(p->buf[1] ^ p->buf[2] ^ p->buf[3] ^ p->buf[4]) != p->buf[5])
        return -1;                              /* header check failed: false SOF */
    uint16_t len = (uint16_t)(p->buf[3] | (p->buf[4] << 8));
    if (len > MRP_MAX_PAYLOAD)
        return -1;
    size_t total = (size_t)MRP_HDR_LEN + len + MRP_CRC_LEN;
    if (p->pos < total)
        return 0;
    uint16_t want = (uint16_t)(p->buf[total - 2] | (p->buf[total - 1] << 8));
    uint16_t got = mrp_crc16(p->buf + 1, (size_t)MRP_HDR_LEN - 1 + len, MRP_CRC_INIT);
    if (want != got) {
        p->crc_errors++;
        return -1;
    }
    p->frames_ok++;
    if (p->cb)
        p->cb(p->user, p->buf[1], p->buf[2], p->buf + MRP_HDR_LEN, len);
    return 1;
}

void mrp_parser_feed(mrp_parser_t *p, const uint8_t *data, size_t n)
{
    while (n--) {
        uint8_t b = *data++;
        if (p->pos == 0 && b != MRP_SOF)
            continue;                       /* idle: wait for a start byte */
        p->buf[p->pos++] = b;
        for (;;) {
            int r = parser_try(p);
            if (r == 0)
                break;
            if (r == 1) {
                p->pos = 0;
                break;
            }
            /* junk: drop one byte, re-examine the remainder (a SOF may hide inside) */
            p->resyncs++;
            memmove(p->buf, p->buf + 1, p->pos - 1);
            p->pos--;
            while (p->pos && p->buf[0] != MRP_SOF) {
                memmove(p->buf, p->buf + 1, p->pos - 1);
                p->pos--;
            }
            if (p->pos == 0)
                break;
        }
    }
}

/* ------------------------------------------------- little-endian helpers */
static void put16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v) { put16(b, (uint16_t)v); put16(b + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)get16(b) | ((uint32_t)get16(b + 2) << 16); }

/* ---------------------------------------------------------------- pack */
size_t mrp_pack_set_channel(const mrp_set_channel_t *m, uint8_t *buf)
{
    buf[0] = m->channel; buf[1] = m->bandwidth; buf[2] = m->ctcss_tx; buf[3] = m->ctcss_rx;
    buf[4] = m->squelch; buf[5] = m->power; buf[6] = m->flags;
    return MRP_SET_CHANNEL_LEN;
}
size_t mrp_pack_ptt(const mrp_ptt_t *m, uint8_t *buf)
{
    buf[0] = m->state; put16(buf + 1, m->ttl_ms);
    return MRP_PTT_LEN;
}
size_t mrp_pack_set_audio(const mrp_set_audio_t *m, uint8_t *buf)
{
    buf[0] = m->volume; buf[1] = m->mic_gain; buf[2] = m->filter_flags;
    return MRP_SET_AUDIO_LEN;
}
size_t mrp_pack_set_power(const mrp_set_power_t *m, uint8_t *buf)
{
    buf[0] = m->flags; buf[1] = m->max_source_w; buf[2] = m->reserve_soc_pct;
    return MRP_SET_POWER_LEN;
}
size_t mrp_pack_ack(const mrp_ack_t *m, uint8_t *buf)
{
    buf[0] = m->req_type; buf[1] = m->result; put16(buf + 2, m->detail);
    return MRP_ACK_LEN;
}
size_t mrp_pack_info(const mrp_info_t *m, uint8_t *buf)
{
    buf[0] = m->proto; buf[1] = m->fw_major; buf[2] = m->fw_minor; buf[3] = m->hw_rev;
    put32(buf + 4, m->caps);
    memcpy(buf + 8, m->serial, 16);
    buf[24] = m->region; buf[25] = m->n_channels;
    return MRP_INFO_LEN;
}
size_t mrp_pack_status(const mrp_status_t *m, uint8_t *buf)
{
    put16(buf + 0, m->flags);
    buf[2] = (uint8_t)m->rssi_dbm;
    buf[3] = m->channel;
    put16(buf + 4, m->batt_mv);
    put16(buf + 6, (uint16_t)m->batt_ma);
    buf[8] = m->soc_pct;
    buf[9] = (uint8_t)m->temp_c;
    put16(buf + 10, m->vbus_mv);
    put16(buf + 12, (uint16_t)m->vbus_ma);
    buf[14] = m->pd_role;
    put16(buf + 15, m->pd_mv);
    put16(buf + 17, m->pd_ma);
    put16(buf + 19, m->tx_remaining_s);
    buf[21] = m->mode; buf[22] = m->profile; buf[23] = m->dv_flags; buf[24] = m->pwr_flags;
    put32(buf + 25, m->freq_hz);
    put16(buf + 29, m->in_mv);
    put16(buf + 31, m->in_ma);
    return MRP_STATUS_LEN;
}

static void put_str(uint8_t *b, const char *s, size_t n)
{
    memset(b, 0, n);
    if (s) strncpy((char *)b, s, n);
}
static void get_str(const uint8_t *b, char *s, size_t n)   /* s has n+1 bytes */
{
    memcpy(s, b, n);
    s[n] = '\0';
}

size_t mrp_pack_set_freq(const mrp_set_freq_t *m, uint8_t *buf)
{
    put32(buf, m->hz); buf[4] = m->squelch; buf[5] = m->power; buf[6] = m->ctcss_tx; buf[7] = m->ctcss_rx; buf[8] = m->bandwidth;
    return MRP_SET_FREQ_LEN;
}
size_t mrp_pack_set_identity(const mrp_set_identity_t *m, uint8_t *buf)
{
    put_str(buf, m->callsign, 8); buf[8] = m->ssid; put_str(buf + 9, m->nickname, 8);
    return MRP_SET_IDENTITY_LEN;
}
size_t mrp_pack_set_beacon(const mrp_set_beacon_t *m, uint8_t *buf)
{
    buf[0] = m->enabled; buf[1] = m->smart; put16(buf + 2, m->interval_s);
    buf[4] = (uint8_t)m->sym_table; buf[5] = (uint8_t)m->sym_code; buf[6] = m->transport;
    put_str(buf + 7, m->path, 16); put_str(buf + 23, m->comment, 32);
    return MRP_SET_BEACON_LEN;
}
size_t mrp_pack_position(const mrp_position_t *m, uint8_t *buf)
{
    put32(buf, (uint32_t)m->lat_e7); put32(buf + 4, (uint32_t)m->lon_e7);
    put16(buf + 8, (uint16_t)m->alt_m); put16(buf + 10, m->speed_kmh); put16(buf + 12, (uint16_t)m->course_deg);
    buf[14] = m->valid;
    return MRP_POSITION_LEN;
}
size_t mrp_pack_msg_status(const mrp_msg_status_t *m, uint8_t *buf)
{
    put16(buf, m->id); buf[2] = m->state;
    return MRP_MSG_STATUS_LEN;
}
bool mrp_unpack_set_freq(const uint8_t *buf, uint16_t len, mrp_set_freq_t *m)
{
    if (len != MRP_SET_FREQ_LEN) return false;
    m->hz = get32(buf); m->squelch = buf[4]; m->power = buf[5]; m->ctcss_tx = buf[6]; m->ctcss_rx = buf[7]; m->bandwidth = buf[8];
    return true;
}
bool mrp_unpack_set_identity(const uint8_t *buf, uint16_t len, mrp_set_identity_t *m)
{
    if (len != MRP_SET_IDENTITY_LEN) return false;
    memcpy(m->callsign, buf, 8); m->callsign[7] = '\0'; m->ssid = buf[8];
    memcpy(m->nickname, buf + 9, 8); m->nickname[7] = '\0';
    return true;
}
bool mrp_unpack_set_beacon(const uint8_t *buf, uint16_t len, mrp_set_beacon_t *m)
{
    if (len != MRP_SET_BEACON_LEN) return false;
    m->enabled = buf[0]; m->smart = buf[1]; m->interval_s = get16(buf + 2);
    m->sym_table = (char)buf[4]; m->sym_code = (char)buf[5]; m->transport = buf[6];
    memcpy(m->path, buf + 7, 16); m->path[15] = '\0';
    memcpy(m->comment, buf + 23, 32); m->comment[31] = '\0';
    return true;
}
bool mrp_unpack_position(const uint8_t *buf, uint16_t len, mrp_position_t *m)
{
    if (len != MRP_POSITION_LEN) return false;
    m->lat_e7 = (int32_t)get32(buf); m->lon_e7 = (int32_t)get32(buf + 4);
    m->alt_m = (int16_t)get16(buf + 8); m->speed_kmh = get16(buf + 10); m->course_deg = (int16_t)get16(buf + 12);
    m->valid = buf[14];
    return true;
}
bool mrp_unpack_msg_status(const uint8_t *buf, uint16_t len, mrp_msg_status_t *m)
{
    if (len != MRP_MSG_STATUS_LEN) return false;
    m->id = get16(buf); m->state = buf[2];
    return true;
}

size_t mrp_pack_send_msg(uint8_t *buf, size_t cap, uint8_t transport, const char *to, const char *text)
{
    size_t tl = text ? strlen(text) : 0;
    if (!to || tl == 0 || tl > 67 || cap < MRP_SEND_MSG_HDR + tl) return 0;
    buf[0] = transport; put_str(buf + 1, to, 10); memcpy(buf + MRP_SEND_MSG_HDR, text, tl);
    return MRP_SEND_MSG_HDR + tl;
}
bool mrp_unpack_send_msg(const uint8_t *buf, uint16_t len, uint8_t *transport, char to[11], char text[68])
{
    if (len <= MRP_SEND_MSG_HDR || len > MRP_SEND_MSG_HDR + 67) return false;
    *transport = buf[0]; get_str(buf + 1, to, 10);
    size_t tl = len - MRP_SEND_MSG_HDR;
    memcpy(text, buf + MRP_SEND_MSG_HDR, tl); text[tl] = '\0';
    return true;
}
size_t mrp_pack_msg_rx(uint8_t *buf, size_t cap, uint8_t transport, const char *from, const char *id, const char *text)
{
    size_t tl = text ? strlen(text) : 0;
    if (tl > 67 || cap < MRP_MSG_RX_HDR + tl) return 0;
    buf[0] = transport; put_str(buf + 1, from, 10); put_str(buf + 11, id, 6); memcpy(buf + MRP_MSG_RX_HDR, text, tl);
    return MRP_MSG_RX_HDR + tl;
}
size_t mrp_pack_aprs_rx(uint8_t *buf, size_t cap, uint8_t transport, const char *src, uint8_t kind,
                        int32_t lat_e7, int32_t lon_e7, int16_t course, uint16_t speed, const char *text)
{
    size_t tl = text ? strlen(text) : 0;
    if (tl > 67) tl = 67;
    if (cap < MRP_APRS_RX_HDR + tl) return 0;
    buf[0] = transport; put_str(buf + 1, src, 10); buf[11] = kind;
    put32(buf + 12, (uint32_t)lat_e7); put32(buf + 16, (uint32_t)lon_e7);
    put16(buf + 20, (uint16_t)course); put16(buf + 22, speed);
    if (tl) memcpy(buf + MRP_APRS_RX_HDR, text, tl);
    return MRP_APRS_RX_HDR + tl;
}

/* -------------------------------------------------------------- unpack */
bool mrp_unpack_set_channel(const uint8_t *buf, uint16_t len, mrp_set_channel_t *m)
{
    if (len != MRP_SET_CHANNEL_LEN) return false;
    m->channel = buf[0]; m->bandwidth = buf[1]; m->ctcss_tx = buf[2]; m->ctcss_rx = buf[3];
    m->squelch = buf[4]; m->power = buf[5]; m->flags = buf[6];
    return true;
}
bool mrp_unpack_ptt(const uint8_t *buf, uint16_t len, mrp_ptt_t *m)
{
    if (len != MRP_PTT_LEN) return false;
    m->state = buf[0]; m->ttl_ms = get16(buf + 1);
    return true;
}
bool mrp_unpack_set_audio(const uint8_t *buf, uint16_t len, mrp_set_audio_t *m)
{
    if (len != MRP_SET_AUDIO_LEN) return false;
    m->volume = buf[0]; m->mic_gain = buf[1]; m->filter_flags = buf[2];
    return true;
}
bool mrp_unpack_set_power(const uint8_t *buf, uint16_t len, mrp_set_power_t *m)
{
    if (len != MRP_SET_POWER_LEN) return false;
    m->flags = buf[0]; m->max_source_w = buf[1]; m->reserve_soc_pct = buf[2];
    return true;
}
bool mrp_unpack_ack(const uint8_t *buf, uint16_t len, mrp_ack_t *m)
{
    if (len != MRP_ACK_LEN) return false;
    m->req_type = buf[0]; m->result = buf[1]; m->detail = get16(buf + 2);
    return true;
}
bool mrp_unpack_info(const uint8_t *buf, uint16_t len, mrp_info_t *m)
{
    if (len != MRP_INFO_LEN) return false;
    m->proto = buf[0]; m->fw_major = buf[1]; m->fw_minor = buf[2]; m->hw_rev = buf[3];
    m->caps = get32(buf + 4);
    memcpy(m->serial, buf + 8, 16);
    m->region = buf[24]; m->n_channels = buf[25];
    return true;
}
bool mrp_unpack_status(const uint8_t *buf, uint16_t len, mrp_status_t *m)
{
    if (len != MRP_STATUS_LEN) return false;
    m->mode = buf[21]; m->profile = buf[22]; m->dv_flags = buf[23]; m->pwr_flags = buf[24];
    m->freq_hz = get32(buf + 25);
    m->in_mv = get16(buf + 29);
    m->in_ma = get16(buf + 31);
    m->flags = get16(buf);
    m->rssi_dbm = (int8_t)buf[2];
    m->channel = buf[3];
    m->batt_mv = get16(buf + 4);
    m->batt_ma = (int16_t)get16(buf + 6);
    m->soc_pct = buf[8];
    m->temp_c = (int8_t)buf[9];
    m->vbus_mv = get16(buf + 10);
    m->vbus_ma = (int16_t)get16(buf + 12);
    m->pd_role = buf[14];
    m->pd_mv = get16(buf + 15);
    m->pd_ma = get16(buf + 17);
    m->tx_remaining_s = get16(buf + 19);
    return true;
}
