/*
 * mobile-radio application.
 *
 * Super-loop at ~1 kHz; audio moves on DMA rings at 48 kHz.
 *
 *   USB bulk (MRP) --> parser --> handlers (channel/freq/mode/identity/beacon/msg/dv/profile)
 *   ADC 48 kHz --> [DV: freedv rx] or [/3 -> 16 kHz -> AUDIO_RX to phone + AFSK demod -> AX.25 -> APRS/msg]
 *   AUDIO_TX from phone --> [DV: freedv tx] or [x3 -> 48 kHz] --> DAC (only while keyed)
 *   data TX: AX.25 -> HDLC -> AFSK (generated chunk-wise) --> DAC, keyed through ptt_guard as PTT_SRC_DATA
 *   1 Hz: power status (two ports), PD policy with pass-through, beacons, message retries, STATUS frame
 *
 * Vendor HAL / TinyUSB glue lives in port/<target>/; nothing here touches a register.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "afsk.h"
#include "aprs.h"
#include "arbiter.h"
#include "ax25.h"
#include "bandplan.h"
#include "beacon.h"
#include "dv.h"
#include "hal.h"
#include "mrp.h"
#include "msg.h"
#include "pd_policy.h"
#include "pmr446.h"
#include "ptt_guard.h"
#include "resample.h"
#include "sa818.h"

#define FW_MAJOR 0
#define FW_MINOR 2
#define HW_REV   1

/* Measured antenna gain (production test) decides the PMR446 power level. */
#ifndef ANTENNA_GAIN_DBD_X10
#define ANTENNA_GAIN_DBD_X10 (-40)
#endif
#define FRONT_END_LOSS_DB_X10 11
#define ERP_X10_AT_1W (300 - FRONT_END_LOSS_DB_X10 + ANTENNA_GAIN_DBD_X10)

#define APRS_TOCALL     "APZMRP"
#define DATA_TXDELAY_FLAGS 45     /* 300 ms */
#define DATA_TAIL_FLAGS    3
#define VOICE_FRAME_48K    (MRP_AUDIO_FRAME_SAMPLES * 3)

/* ------------------------------------------------------------- state */
static mrp_parser_t g_parser;
static pd_policy_t  g_pd;
static ptt_guard_t  g_ptt;
static bandplan_t   g_band;
static arbiter_t    g_arb;
static msg_ctx_t    g_msg;
static beacon_t     g_beacon;
static resample_t   g_rs_down, g_rs_up;
static afsk_demod_t g_demod;
static hdlc_rx_t    g_hdlc;
static dv_t         g_dv;

static mrp_set_channel_t g_chan = { .channel = 1, .squelch = 4 };
static uint32_t     g_freq_hz = PMR446_CH1_HZ;
static uint8_t      g_squelch = 4, g_power = 0, g_ctcss_tx, g_ctcss_rx;
static uint8_t      g_profile = MRP_PROFILE_APP;
static bool         g_dv_on;
static mrp_set_beacon_t g_beacon_cfg = { .sym_table = '/', .sym_code = '[', .transport = MRP_TR_FM, .path = "WIDE1-1" };
static mrp_position_t g_pos;

static uint8_t      g_seq;
static uint32_t     g_last_host_ms;
static bool         g_radio_ok;
static bool         g_keyed;               /* PA keyed right now */
static uint8_t      g_txbuf[MRP_MAX_FRAME];
static int16_t      g_pcm48[VOICE_FRAME_48K + 64];
static int16_t      g_pcm16[MRP_AUDIO_FRAME_SAMPLES + 32];
static int16_t      g_dvmodem[DV_MODEM_MAX];

/* data (AFSK) transmission in progress */
static struct {
    bool     active;
    uint8_t  levels[6000];
    size_t   nbits, pos;
    afsk_mod_t mod;
    bool     draining;
} g_data;

bool app_send_info(uint8_t transport, const char *info, uint32_t now);   /* defined below */

/* ------------------------------------------------------------- util */
static void send(uint8_t type, const void *payload, uint16_t len)
{
    size_t n = mrp_encode(g_txbuf, sizeof g_txbuf, type, g_seq++, payload, len);
    if (n && hal_usb_connected()) hal_usb_write(g_txbuf, n);
}

static void ack(uint8_t req, uint8_t result, uint16_t detail)
{
    mrp_ack_t a = { req, result, detail };
    uint8_t b[MRP_ACK_LEN];
    send(MRP_ACK, b, (uint16_t)mrp_pack_ack(&a, b));
}

/* ------------------------------------------------------------ radio */
static bool radio_cmd(const char *cmd, size_t len, sa818_resp_kind_t expect)
{
    char line[64];
    hal_radio_uart_write(cmd, len);
    for (int tries = 0; tries < 3; tries++) {
        size_t n = hal_radio_uart_read_line(line, sizeof line, 300);
        if (!n) continue;
        sa818_resp_t r;
        if (sa818_parse_response(line, &r) && r.kind == expect)
            return r.code == 0;
    }
    return false;
}

static bool radio_apply(void)
{
    char cmd[64];
    sa818_group_t g = { .bandwidth = 0, .tx_hz = g_freq_hz, .rx_hz = g_freq_hz,
                        .ctcss_tx = g_ctcss_tx, .ctcss_rx = g_ctcss_rx, .squelch = g_squelch };
    int n = sa818_cmd_setgroup(cmd, sizeof cmd, &g);
    if (!n || !radio_cmd(cmd, (size_t)n, SA818_RESP_SETGROUP))
        return false;
    hal_radio_high_power(g_power == 1 && bandplan_high_power_allowed(&g_band, ERP_X10_AT_1W));
    return true;
}

static bool radio_set_filters(bool bypass_emphasis)
{
    char cmd[64];
    int n = sa818_cmd_filter(cmd, sizeof cmd, bypass_emphasis, false, false);
    return radio_cmd(cmd, (size_t)n, SA818_RESP_SETFILTER);
}

static bool radio_init(void)
{
    char cmd[64];
    hal_radio_power(true);
    hal_delay_ms(500);
    int n = sa818_cmd_connect(cmd, sizeof cmd);
    if (!radio_cmd(cmd, (size_t)n, SA818_RESP_CONNECT))
        return false;
    radio_set_filters(false);
    n = sa818_cmd_volume(cmd, sizeof cmd, 4);
    radio_cmd(cmd, (size_t)n, SA818_RESP_SETVOLUME);
    return radio_apply();
}

/* ----------------------------------------------------- APRS reception */
static void aprs_rx_handle(uint8_t transport, const char *src, const char *info, size_t len, int16_t rssi)
{
    (void)rssi;
    uint8_t b[MRP_MAX_PAYLOAD];
    msg_rx_t rx; char ackinfo[64];
    bool want_ack = msg_on_rx(&g_msg, bandplan_identity(&g_band), src, info, len, &rx, ackinfo, sizeof ackinfo);
    if (rx.valid)
        send(MRP_MSG_RX, b, (uint16_t)mrp_pack_msg_rx(b, sizeof b, transport, rx.from, rx.id, rx.text));
    if (want_ack)
        app_send_info(transport, ackinfo, hal_millis());   /* ack on the transport it came from */
    aprs_packet_t pk;
    if (aprs_decode(info, len, &pk) && (pk.kind == APRS_KIND_POSITION || pk.kind == APRS_KIND_STATUS)) {
        int32_t lat = (int32_t)(pk.lat * 1e7), lon = (int32_t)(pk.lon * 1e7);
        uint16_t kmh = pk.speed_knots > 0 ? (uint16_t)(pk.speed_knots * 1.852) : 0;
        send(MRP_APRS_RX, b, (uint16_t)mrp_pack_aprs_rx(b, sizeof b, transport, src, (uint8_t)pk.kind, lat, lon,
                                                        (int16_t)pk.course_deg, kmh, pk.comment));
    }
}

static void on_ax25_frame(void *user, const uint8_t *frame, size_t len)
{
    (void)user;
    ax25_frame_t f;
    if (!ax25_parse(frame, len, &f)) return;
    uint8_t b[MRP_MAX_PAYLOAD];
    if (len <= sizeof b) { memcpy(b, frame, len); send(MRP_DATA_RX, b, (uint16_t)len); }
    char src[10]; ax25_addr_to_str(&f.src, src, sizeof src);
    char info[AX25_MAX_INFO + 1];
    size_t il = f.info_len > AX25_MAX_INFO ? AX25_MAX_INFO : f.info_len;
    memcpy(info, f.info, il); info[il] = '\0';
    aprs_rx_handle(MRP_TR_FM, src, info, il, 0);
}

/* ----------------------------------------------------- data transmit */
static bool data_tx_start(const char *info, uint32_t now)
{
    if (g_data.active) return false;
    ax25_addr_t dst, src, path[2]; int np = 0;
    ax25_addr_from_str(&dst, APRS_TOCALL);
    char me[16];
    if (g_band.mode == BAND_MODE_HAM_70CM) snprintf(me, sizeof me, "%s-%u", g_band.callsign, g_band.ssid);
    else snprintf(me, sizeof me, "%s", g_band.nickname);
    if (!ax25_addr_from_str(&src, me)) return false;
    if (g_beacon_cfg.path[0]) {
        char tmp[16]; strncpy(tmp, g_beacon_cfg.path, sizeof tmp - 1); tmp[sizeof tmp - 1] = '\0';
        char *p = tmp;
        while (p && *p && np < 2) {
            char *c = strchr(p, ',');
            if (c) *c = '\0';
            if (ax25_addr_from_str(&path[np], p)) np++;
            p = c ? c + 1 : NULL;
        }
    }
    uint8_t frame[AX25_MAX_FRAME];
    size_t fl = ax25_build_ui(frame, sizeof frame, &dst, &src, path, np, (const uint8_t *)info, strlen(info));
    if (!fl) return false;
    size_t nb = hdlc_encode(frame, fl, DATA_TXDELAY_FLAGS, DATA_TAIL_FLAGS, g_data.levels, sizeof g_data.levels, NULL);
    if (!nb) return false;

    tx_kind_t pre;
    if (arbiter_request(&g_arb, TX_FM_DATA, now, &pre) == ARB_BUSY) return false;
    ptt_guard_set_channel_busy(&g_ptt, hal_radio_squelch_open());
    if (ptt_guard_request(&g_ptt, PTT_SRC_DATA, true, now) != PTT_KEYED) {
        arbiter_release(&g_arb, TX_FM_DATA);
        return false;
    }
    afsk_mod_init(&g_data.mod, HAL_AUDIO_RATE, 9000);
    g_data.nbits = nb; g_data.pos = 0; g_data.active = true; g_data.draining = false;
    return true;
}

static void data_tx_abort(void)
{
    g_data.active = false;
    arbiter_release(&g_arb, TX_FM_DATA);
}

static void data_tx_service(uint32_t now)
{
    if (!g_data.active) return;
    if (g_ptt.source != PTT_SRC_DATA || g_ptt.state != PTT_KEYED) {   /* pre-empted or dropped */
        data_tx_abort();
        return;
    }
    if (!g_keyed) return;                                              /* PA not up yet */
    if (g_data.pos < g_data.nbits) {
        size_t room = hal_audio_tx_free();
        size_t bits = room / (HAL_AUDIO_RATE / AFSK_BAUD + 1);
        if (bits > 64) bits = 64;
        if (bits > g_data.nbits - g_data.pos) bits = g_data.nbits - g_data.pos;
        if (bits) {
            size_t ns = afsk_mod_run(&g_data.mod, g_data.levels + g_data.pos, bits, g_pcm48, sizeof g_pcm48 / sizeof g_pcm48[0]);
            if (ns) { hal_audio_tx_write(g_pcm48, ns); g_data.pos += bits; }
        }
        return;
    }
    if (hal_audio_tx_pending() == 0) {
        ptt_guard_request(&g_ptt, PTT_SRC_DATA, false, now);
        data_tx_abort();
    }
}

/* Route an APRS info field to a transport (only FM exists now; the parameter stays for the wire format). */
bool app_send_info(uint8_t transport, const char *info, uint32_t now)
{
    (void)transport;
    return data_tx_start(info, now);
}

/* ------------------------------------------------------- PD glue */
static void pd_apply(pd_action_t a)
{
    switch (a.kind) {
    case PD_ACT_REQUEST_PR_SWAP:   hal_pd_request_pr_swap(); break;
    case PD_ACT_REQUEST_DR_SWAP:   hal_pd_request_dr_swap(); break;
    case PD_ACT_ACCEPT_PR_SWAP:    hal_pd_reply_pr_swap(true); break;
    case PD_ACT_REJECT_PR_SWAP:    hal_pd_reply_pr_swap(false); break;
    case PD_ACT_SET_SINK_LIMIT_MA: hal_power_set_sink_limit_ma(a.arg); break;
    case PD_ACT_ENABLE_SOURCE:     hal_power_source_enable(true, (uint8_t)a.arg); break;
    case PD_ACT_DISABLE_SOURCE:    hal_power_source_enable(false, 0); break;
    default: break;
    }
}

void app_pd_event(const pd_event_t *ev)
{
    pd_apply(pd_policy_event(&g_pd, ev, hal_millis()));
}

/* ------------------------------------------------------ settings */
static void set_profile(uint8_t profile)
{
    g_profile = profile;
    hal_audio_usb_direct(profile == MRP_PROFILE_SOUNDCARD);
    hal_usb_set_profile(profile);
}

static bool apply_mode(band_mode_t mode)
{
    if (!bandplan_set_mode(&g_band, mode)) return false;
    g_freq_hz = bandplan_default_hz(mode);
    if (mode == BAND_MODE_PMR446) {
        g_chan.channel = 1; g_power = 0;
        if (g_dv_on) { dv_close(&g_dv); g_dv_on = false; radio_set_filters(false); }
    }
    return radio_apply();
}

/* --------------------------------------------------------- MRP handlers */
static void on_frame(void *user, uint8_t type, uint8_t seq, const uint8_t *pl, uint16_t len)
{
    (void)user; (void)seq;
    uint32_t now = hal_millis();
    g_last_host_ms = now;
    ptt_guard_keepalive(&g_ptt, now);

    switch (type) {
    case MRP_GET_INFO: {
        mrp_info_t i = { .proto = MRP_PROTO_VERSION, .fw_major = FW_MAJOR, .fw_minor = FW_MINOR, .hw_rev = HW_REV,
                         .caps = MRP_CAP_VOICE | MRP_CAP_DATA | MRP_CAP_PD_SRC | MRP_CAP_PD_SNK | MRP_CAP_UAC,
                         .region = REGION_EU_PMR446, .n_channels = PMR446_CHANNELS };
        strncpy(i.serial, "MR-PROTO-0001", sizeof i.serial);
        uint8_t b[MRP_INFO_LEN];
        send(MRP_INFO, b, (uint16_t)mrp_pack_info(&i, b));
        break;
    }
    case MRP_SET_CHANNEL: {
        mrp_set_channel_t c;
        if (!mrp_unpack_set_channel(pl, len, &c)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        if (g_band.mode != BAND_MODE_PMR446 ||
            !pmr446_validate(c.channel, c.bandwidth, c.ctcss_tx, c.ctcss_rx, c.squelch, c.power)) {
            ack(type, MRP_ERR_BAD_VALUE, 0); break;
        }
        if (g_ptt.state == PTT_KEYED) { ack(type, MRP_ERR_BUSY, 0); break; }
        g_freq_hz = pmr446_channel_hz(c.channel); g_squelch = c.squelch; g_power = c.power;
        g_ctcss_tx = c.ctcss_tx; g_ctcss_rx = c.ctcss_rx;
        g_ptt.cfg.busy_lockout = (c.flags & MRP_CHAN_FLAG_BCLO) != 0;
        if (!radio_apply()) { ack(type, MRP_ERR_RADIO, 0); break; }
        g_chan = c;
        ack(type, MRP_OK, 0);
        break;
    }
    case MRP_SET_FREQ: {
        mrp_set_freq_t f;
        if (!mrp_unpack_set_freq(pl, len, &f)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        if (g_band.mode != BAND_MODE_HAM_70CM) { ack(type, MRP_ERR_INHIBITED, 0); break; }
        if (!bandplan_valid_hz(&g_band, f.hz) || f.squelch > 8 || f.power > 1 || f.ctcss_tx > CTCSS_TONES ||
            f.ctcss_rx > CTCSS_TONES || f.bandwidth > 1) { ack(type, MRP_ERR_BAD_VALUE, 0); break; }
        if (g_ptt.state == PTT_KEYED) { ack(type, MRP_ERR_BUSY, 0); break; }
        g_freq_hz = f.hz; g_squelch = f.squelch; g_power = f.power; g_ctcss_tx = f.ctcss_tx; g_ctcss_rx = f.ctcss_rx;
        ack(type, radio_apply() ? MRP_OK : MRP_ERR_RADIO, 0);
        break;
    }
    case MRP_SET_MODE: {
        if (len != MRP_SET_MODE_LEN) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        if (g_ptt.state == PTT_KEYED) { ack(type, MRP_ERR_BUSY, 0); break; }
        if (!apply_mode((band_mode_t)pl[0])) { ack(type, MRP_ERR_INHIBITED, 0); break; }   /* locked or bad */
        ack(type, MRP_OK, 0);
        break;
    }
    case MRP_SET_IDENTITY: {
        mrp_set_identity_t id;
        if (!mrp_unpack_set_identity(pl, len, &id)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        bool ok = true;
        if (id.callsign[0]) ok = bandplan_set_callsign(&g_band, id.callsign, id.ssid);
        if (ok && id.nickname[0]) ok = bandplan_set_nickname(&g_band, id.nickname);
        ack(type, ok ? MRP_OK : MRP_ERR_BAD_VALUE, g_band.unlocked);
        break;
    }
    case MRP_SET_BEACON: {
        mrp_set_beacon_t bc;
        if (!mrp_unpack_set_beacon(pl, len, &bc)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        if (bc.transport != MRP_TR_FM) { ack(type, MRP_ERR_BAD_VALUE, 0); break; }
        g_beacon_cfg = bc;
        g_beacon.cfg.enabled = bc.enabled != 0;
        g_beacon.cfg.smart = bc.smart != 0;
        if (bc.interval_s) g_beacon.cfg.fixed_interval_s = bc.interval_s;
        ack(type, MRP_OK, 0);
        break;
    }
    case MRP_POSITION: {
        mrp_position_t p;
        if (!mrp_unpack_position(pl, len, &p)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        g_pos = p;
        beacon_pos_t bp = { .lat = p.lat_e7 / 1e7, .lon = p.lon_e7 / 1e7, .alt_m = p.alt_m,
                            .speed_kmh = p.speed_kmh, .course_deg = p.course_deg, .valid = p.valid != 0 };
        beacon_update_position(&g_beacon, &bp);
        break;
    }
    case MRP_SEND_MSG: {
        uint8_t tr; char to[11], text[68];
        if (!mrp_unpack_send_msg(pl, len, &tr, to, text)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        if (tr != MRP_TR_FM) { ack(type, MRP_ERR_BAD_VALUE, 0); break; }
        uint16_t id = msg_send(&g_msg, to, text, now);
        ack(type, id ? MRP_OK : MRP_ERR_BUSY, id);
        break;
    }
    case MRP_SET_PROFILE: {
        if (len != MRP_SET_PROFILE_LEN || pl[0] > 1) { ack(type, MRP_ERR_BAD_VALUE, 0); break; }
        ack(type, MRP_OK, 0);
        set_profile(pl[0]);
        break;
    }
    case MRP_SET_DV: {
        if (len != MRP_SET_DV_LEN) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        bool on = pl[0] != 0;
        if (on && g_band.mode != BAND_MODE_HAM_70CM) { ack(type, MRP_ERR_INHIBITED, 0); break; }
        if (on && !g_dv_on) {
            if (!dv_open(&g_dv)) { ack(type, MRP_ERR_UNSUPPORTED, 0); break; }
            radio_set_filters(true);                /* flat path for the modem */
            hal_audio_flat_path(true);
        } else if (!on && g_dv_on) {
            dv_close(&g_dv);
            radio_set_filters(false);
            hal_audio_flat_path(false);
        }
        g_dv_on = on;
        ack(type, MRP_OK, 0);
        break;
    }
    case MRP_PTT: {
        mrp_ptt_t p;
        if (!mrp_unpack_ptt(pl, len, &p)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        if (p.ttl_ms) g_ptt.cfg.keepalive_ms = p.ttl_ms;
        ptt_guard_set_channel_busy(&g_ptt, hal_radio_squelch_open());
        ptt_state_t st = ptt_guard_request(&g_ptt, PTT_SRC_HOST, p.state != 0, now);
        uint8_t res = (st == PTT_KEYED || st == PTT_IDLE) ? MRP_OK :
                      (st == PTT_DENIED_BUSY) ? MRP_ERR_BUSY : MRP_ERR_INHIBITED;
        ack(type, res, (uint16_t)st);
        break;
    }
    case MRP_SET_AUDIO: {
        mrp_set_audio_t a; char cmd[64]; int n;
        if (!mrp_unpack_set_audio(pl, len, &a)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        n = sa818_cmd_volume(cmd, sizeof cmd, a.volume);
        if (!n || !radio_cmd(cmd, (size_t)n, SA818_RESP_SETVOLUME)) { ack(type, MRP_ERR_RADIO, 1); break; }
        n = sa818_cmd_filter(cmd, sizeof cmd, a.filter_flags & 1, a.filter_flags & 2, a.filter_flags & 4);
        if (!radio_cmd(cmd, (size_t)n, SA818_RESP_SETFILTER)) { ack(type, MRP_ERR_RADIO, 2); break; }
        ack(type, MRP_OK, 0);
        break;
    }
    case MRP_SET_POWER: {
        mrp_set_power_t pw;
        if (!mrp_unpack_set_power(pl, len, &pw)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        g_pd.cfg.allow_source = (pw.flags & MRP_PWR_FLAG_SOURCE) != 0;
        g_pd.cfg.allow_sink = (pw.flags & MRP_PWR_FLAG_CHARGE) != 0;
        if (pw.max_source_w) g_pd.cfg.max_source_w = pw.max_source_w;
        if (pw.reserve_soc_pct) {
            g_pd.cfg.reserve_soc_pct = pw.reserve_soc_pct;
            g_pd.cfg.resume_soc_pct = (uint8_t)(pw.reserve_soc_pct + 10);
        }
        ack(type, MRP_OK, 0);
        break;
    }
    case MRP_AUDIO_TX:
        if (len == MRP_AUDIO_FRAME_BYTES && g_ptt.state == PTT_KEYED && g_ptt.source == PTT_SRC_HOST) {
            memcpy(g_pcm16, pl, len);
            if (g_dv_on) {
                size_t n = dv_tx_push(&g_dv, g_pcm16, MRP_AUDIO_FRAME_SAMPLES, g_dvmodem, DV_MODEM_MAX);
                if (n) hal_audio_tx_write(g_dvmodem, n);
            } else {
                size_t n = resample_16_to_48(&g_rs_up, g_pcm16, MRP_AUDIO_FRAME_SAMPLES, g_pcm48);
                hal_audio_tx_write(g_pcm48, n);
            }
        }
        break;
    case MRP_KEEPALIVE:
    case MRP_GET_STATUS:
        break;
    case MRP_DATA_TX:
        if (len && len <= AX25_MAX_INFO) {
            char info[AX25_MAX_INFO + 1]; memcpy(info, pl, len); info[len] = '\0';
            ack(type, data_tx_start(info, now) ? MRP_OK : MRP_ERR_BUSY, 0);
        } else ack(type, MRP_ERR_BAD_LEN, 0);
        break;
    default:
        ack(type, MRP_ERR_UNSUPPORTED, 0);
    }
}

/* ------------------------------------------------------ audio RX path */
static void audio_rx_service(void)
{
    size_t n = hal_audio_rx_read(g_pcm48, VOICE_FRAME_48K);
    if (n == 0 || g_keyed) return;
    if (g_dv_on) {
        size_t out = dv_rx_push(&g_dv, g_pcm48, n, g_pcm16, sizeof g_pcm16 / sizeof g_pcm16[0]);
        if (out && hal_usb_connected()) send(MRP_AUDIO_RX, g_pcm16, (uint16_t)(out * 2));
        return;
    }
    size_t n16 = resample_48_to_16(&g_rs_down, g_pcm48, n, g_pcm16);
    if (!n16) return;
    bool sq = hal_radio_squelch_open();
    if (sq || g_squelch == 0) afsk_demod_process(&g_demod, g_pcm16, n16);
    if (sq && hal_usb_connected() && g_profile == MRP_PROFILE_APP && n16 == MRP_AUDIO_FRAME_SAMPLES)
        send(MRP_AUDIO_RX, g_pcm16, MRP_AUDIO_FRAME_BYTES);
}

/* ----------------------------------------------------------- 1 Hz task */
static void status_task(uint32_t now)
{
    hal_power_status_t ps;
    hal_power_read(&ps);
    int8_t pa_t = hal_pa_temp_c();
    int8_t hot = pa_t > ps.temp_c ? pa_t : ps.temp_c;

    ptt_guard_set_inhibit(&g_ptt, hot > 70 || ps.soc_pct < 5 || !g_radio_ok, now);
    /* pass-through: a charger on the CHARGE port keeps the phone supplied regardless of SoC */
    bool ext = ps.charge_attached && ps.in_mv > 4500;
    pd_policy_set_external_power(&g_pd, ext, now);
    pd_apply(pd_policy_tick(&g_pd, now, ps.soc_pct));
    /* thermal: slow the battery charge before touching the phone's supply */
    hal_power_charge_throttle(hot > 60 ? 25 : hot > 50 ? 60 : 100);

    /* beacons */
    if (beacon_due(&g_beacon, now)) {
        aprs_pos_t ap = { .lat = g_pos.lat_e7 / 1e7, .lon = g_pos.lon_e7 / 1e7, .sym_table = g_beacon_cfg.sym_table,
                          .sym_code = g_beacon_cfg.sym_code, .course_deg = g_pos.course_deg,
                          .speed_knots = g_pos.speed_kmh > 0 ? (int)(g_pos.speed_kmh / 1.852) : -1,
                          .alt_ft = g_pos.alt_m ? (int)(g_pos.alt_m * 3.281) : INT_MIN, .comment = g_beacon_cfg.comment };
        char info[APRS_MAX_INFO];
        if (aprs_encode_position(info, sizeof info, &ap) > 0) {
            bool sent = false;
            sent = app_send_info(MRP_TR_FM, info, now);
            if (sent) beacon_mark_sent(&g_beacon, now);
        }
    }
    /* message retries */
    char info[APRS_MAX_INFO];
    uint16_t id = msg_next_tx(&g_msg, now, info, sizeof info);
    if (id) {
        app_send_info(MRP_TR_FM, info, now);
        mrp_msg_status_t ms = { id, (uint8_t)msg_state(&g_msg, id) };
        uint8_t b[MRP_MSG_STATUS_LEN];
        send(MRP_MSG_STATUS, b, (uint16_t)mrp_pack_msg_status(&ms, b));
    }

    mrp_status_t s = {
        .flags = (uint16_t)((g_ptt.state == PTT_KEYED ? MRP_ST_PTT : 0) |
                            (hal_radio_squelch_open() ? MRP_ST_SQ_OPEN : 0) |
                            (g_ptt.inhibit ? MRP_ST_TX_INHIBIT : 0) |
                            (ps.charging ? MRP_ST_CHARGING : 0) |
                            (ps.sourcing ? MRP_ST_SOURCING : 0) |
                            (ps.fault ? MRP_ST_FAULT : 0) |
                            (g_radio_ok ? MRP_ST_RADIO_OK : 0) |
                            (now - g_last_host_ms < 3000 ? MRP_ST_HOST_ALIVE : 0)),
        .rssi_dbm = -127, .channel = g_band.mode == BAND_MODE_PMR446 ? pmr446_channel_from_hz(g_freq_hz) : 0,
        .batt_mv = ps.batt_mv, .batt_ma = ps.batt_ma, .soc_pct = ps.soc_pct, .temp_c = hot,
        .vbus_mv = ps.vbus_mv, .vbus_ma = ps.vbus_ma,
        .pd_role = (uint8_t)((g_pd.power_role == PD_POWER_SOURCE ? MRP_PD_SOURCE : 0) |
                             (g_pd.data_role == PD_DATA_DFP ? MRP_PD_DFP : 0) |
                             (g_pd.partner_pd ? MRP_PD_CONTRACT : 0) |
                             (g_pd.partner_drp ? MRP_PD_PARTNER_DRP : 0)),
        .pd_mv = ps.vbus_mv, .pd_ma = (uint16_t)(ps.vbus_ma < 0 ? -ps.vbus_ma : ps.vbus_ma),
        .tx_remaining_s = ptt_guard_tx_remaining_s(&g_ptt, now),
        .mode = (uint8_t)g_band.mode, .profile = g_profile,
        .dv_flags = (uint8_t)((g_dv_on ? 1 : 0) | (g_dv_on && dv_in_sync(&g_dv) ? 2 : 0)),
        .pwr_flags = (uint8_t)((ps.phone_attached ? MRP_PW_PHONE_ATTACHED : 0) |
                               (ps.charge_attached ? MRP_PW_CHARGE_ATTACHED : 0) |
                               (ps.charger_contract ? MRP_PW_CHARGER_CONTRACT : 0) |
                               (ps.sourcing && ext ? MRP_PW_PASSTHROUGH : 0) |
                               (ps.input_is_phone ? MRP_PW_INPUT_IS_PHONE : 0)),
        .freq_hz = g_freq_hz,
        .in_mv = ps.in_mv, .in_ma = ps.in_ma,
    };
    if (g_ptt.state != PTT_KEYED) {
        char cmd[16], line[32]; sa818_resp_t r;
        int n = sa818_cmd_rssi(cmd, sizeof cmd);
        hal_radio_uart_write(cmd, (size_t)n);
        if (hal_radio_uart_read_line(line, sizeof line, 50) && sa818_parse_response(line, &r) && r.kind == SA818_RESP_RSSI)
            s.rssi_dbm = (int8_t)sa818_rssi_to_dbm(r.rssi_raw, 0.5f, -137.0f);
    }
    uint8_t b[MRP_STATUS_LEN];
    send(MRP_STATUS, b, (uint16_t)mrp_pack_status(&s, b));
    hal_led(0, ps.soc_pct > 20);
    hal_led(1, g_ptt.state == PTT_KEYED);
}

/* ----------------------------------------------------------------- main */
int app_main(void)
{
    uint8_t rx[256];
    uint32_t next_status = 0;
    bool btn_prev = false, rts_prev = false;

    mrp_parser_init(&g_parser, on_frame, NULL);
    pd_policy_init(&g_pd, NULL);
    ptt_guard_init(&g_ptt, NULL);
    bandplan_init(&g_band);
    arbiter_init(&g_arb);
    msg_init(&g_msg);
    beacon_init(&g_beacon, NULL);
    resample_init(&g_rs_down);
    resample_init(&g_rs_up);
    hdlc_rx_init(&g_hdlc, on_ax25_frame, NULL);
    afsk_demod_init(&g_demod, MRP_AUDIO_RATE_HZ, &g_hdlc);
    g_radio_ok = radio_init();
    set_profile(MRP_PROFILE_APP);
    hal_log("radio %s\n", g_radio_ok ? "ready" : "NOT RESPONDING");

    for (;;) {
        uint32_t now = hal_millis();
        hal_watchdog_kick();

        size_t n = hal_usb_read(rx, sizeof rx);
        if (n) mrp_parser_feed(&g_parser, rx, n);

        /* local PTT button */
        bool btn = hal_button_ptt();
        if (btn != btn_prev) {
            ptt_guard_set_channel_busy(&g_ptt, hal_radio_squelch_open());
            ptt_guard_request(&g_ptt, PTT_SRC_LOCAL, btn, now);
            btn_prev = btn;
        }
        /* sound-card profile: CDC RTS keys the transmitter like a Digirig */
        if (g_profile == MRP_PROFILE_SOUNDCARD) {
            bool rts = hal_cdc_rts();
            if (rts) ptt_guard_keepalive(&g_ptt, now);
            if (rts != rts_prev) {
                ptt_guard_set_channel_busy(&g_ptt, hal_radio_squelch_open());
                ptt_guard_request(&g_ptt, PTT_SRC_HOST, rts, now);
                rts_prev = rts;
            }
        }
        if (hal_button_mode_long())
            set_profile(g_profile == MRP_PROFILE_APP ? MRP_PROFILE_SOUNDCARD : MRP_PROFILE_APP);

        /* voice keying goes through the arbiter too, so data yields */
        bool voice_keyed = g_ptt.state == PTT_KEYED && g_ptt.source != PTT_SRC_DATA;
        if (voice_keyed && arbiter_owner(&g_arb) != (g_dv_on ? TX_FM_DV : TX_FM_VOICE)) {
            tx_kind_t pre;
            arbiter_request(&g_arb, g_dv_on ? TX_FM_DV : TX_FM_VOICE, now, &pre);
            if (pre == TX_FM_DATA) data_tx_abort();
        } else if (!voice_keyed && (arbiter_owner(&g_arb) == TX_FM_VOICE || arbiter_owner(&g_arb) == TX_FM_DV)) {
            arbiter_release(&g_arb, arbiter_owner(&g_arb));
        }

        bool key_now = ptt_guard_tick(&g_ptt, now);
        if (key_now != g_keyed) {
            g_keyed = key_now;
            hal_audio_tx_enable(g_keyed);
            hal_radio_ptt(g_keyed);
            hal_audio_local_path(!g_keyed && !hal_usb_connected(), g_keyed && g_ptt.source == PTT_SRC_LOCAL);
        }

        data_tx_service(now);
        audio_rx_service();

        if ((int32_t)(now - next_status) >= 0) {
            status_task(now);
            next_status = now + 1000;
        }
        hal_delay_ms(1);
    }
}
