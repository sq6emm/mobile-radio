/*
 * mobile-radio application skeleton.
 *
 * Super-loop (no RTOS needed): the only hard real-time path is audio, which
 * runs on DMA rings; everything else is polled at 1 kHz.
 *
 *   USB bulk  --> mrp_parser --> handlers --> radio / power / ptt_guard
 *   ADC DMA   --> 20 ms PCM frames --> MRP_AUDIO_RX --> USB bulk
 *   MRP_AUDIO_TX --> DAC DMA ring --> module MIC_IN (only while keyed)
 *   1 Hz      --> power status, pd_policy_tick, MRP_STATUS
 *
 * Vendor HAL / TinyUSB glue lives in port/<target>/; nothing here touches a
 * register directly.
 */
#include <stdio.h>
#include <string.h>

#include "hal.h"
#include "mrp.h"
#include "pd_policy.h"
#include "pmr446.h"
#include "ptt_guard.h"
#include "sa818.h"

#define FW_MAJOR 0
#define FW_MINOR 1
#define HW_REV   1

/* Measured antenna gain decides whether the module may use its 1 W setting
 * and still stay under 500 mW ERP. Set from the production test. */
#ifndef ANTENNA_GAIN_DBD_X10
#define ANTENNA_GAIN_DBD_X10 (-40)   /* -4.0 dBd */
#endif
#define FRONT_END_LOSS_DB_X10 11       /* LPF 0.8 + traces 0.3 */

static mrp_parser_t   g_parser;
static pd_policy_t    g_pd;
static ptt_guard_t    g_ptt;
static mrp_set_channel_t g_chan = { .channel = 1, .squelch = 4 };
static uint8_t        g_seq;
static uint32_t       g_last_host_ms;
static bool           g_radio_ok;
static uint8_t        g_txbuf[MRP_MAX_FRAME];
static int16_t        g_pcm[MRP_AUDIO_FRAME_SAMPLES];

/* ------------------------------------------------------------------ util */
static void send(uint8_t type, const void *payload, uint16_t len)
{
    size_t n = mrp_encode(g_txbuf, sizeof g_txbuf, type, g_seq++, payload, len);
    if (n) hal_usb_write(g_txbuf, n);
}

static void ack(uint8_t req, uint8_t result, uint16_t detail)
{
    mrp_ack_t a = { req, result, detail };
    uint8_t b[MRP_ACK_LEN];
    send(MRP_ACK, b, (uint16_t)mrp_pack_ack(&a, b));
}

/* Highest module power level that keeps ERP <= 500 mW with 0.5 dB margin. */
static bool high_power_allowed(void)
{
    /* 1 W = 30 dBm; ERP = 30 - loss + gain must be <= 26.5 dBm */
    int erp_x10 = 300 - FRONT_END_LOSS_DB_X10 + ANTENNA_GAIN_DBD_X10;
    return erp_x10 <= 265;
}

/* ----------------------------------------------------------------- radio */
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

static bool radio_apply_channel(const mrp_set_channel_t *c)
{
    char cmd[64];
    sa818_group_t g = {
        .bandwidth = c->bandwidth,
        .tx_hz = pmr446_channel_hz(c->channel),
        .rx_hz = pmr446_channel_hz(c->channel),
        .ctcss_tx = c->ctcss_tx, .ctcss_rx = c->ctcss_rx, .squelch = c->squelch,
    };
    int n = sa818_cmd_setgroup(cmd, sizeof cmd, &g);
    if (!n || !radio_cmd(cmd, (size_t)n, SA818_RESP_SETGROUP))
        return false;
    hal_radio_high_power(c->power == 1 && high_power_allowed());
    g_ptt.cfg.busy_lockout = (c->flags & MRP_CHAN_FLAG_BCLO) != 0;
    return true;
}

static bool radio_init(void)
{
    char cmd[64];
    hal_radio_power(true);
    hal_delay_ms(500);
    int n = sa818_cmd_connect(cmd, sizeof cmd);
    if (!radio_cmd(cmd, (size_t)n, SA818_RESP_CONNECT))
        return false;
    n = sa818_cmd_filter(cmd, sizeof cmd, false, false, false);   /* all audio filters active */
    radio_cmd(cmd, (size_t)n, SA818_RESP_SETFILTER);
    n = sa818_cmd_volume(cmd, sizeof cmd, 4);
    radio_cmd(cmd, (size_t)n, SA818_RESP_SETVOLUME);
    return radio_apply_channel(&g_chan);
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
        mrp_info_t i = { .proto = MRP_PROTO_VERSION, .fw_major = FW_MAJOR, .fw_minor = FW_MINOR,
                         .hw_rev = HW_REV, .caps = MRP_CAP_VOICE | MRP_CAP_PD_SRC | MRP_CAP_PD_SNK,
                         .region = REGION_EU_PMR446, .n_channels = PMR446_CHANNELS };
        strncpy(i.serial, "MR-PROTO-0001", sizeof i.serial);
        uint8_t b[MRP_INFO_LEN];
        send(MRP_INFO, b, (uint16_t)mrp_pack_info(&i, b));
        break;
    }
    case MRP_SET_CHANNEL: {
        mrp_set_channel_t c;
        if (!mrp_unpack_set_channel(pl, len, &c)) { ack(type, MRP_ERR_BAD_LEN, 0); break; }
        if (!pmr446_validate(c.channel, c.bandwidth, c.ctcss_tx, c.ctcss_rx, c.squelch, c.power)) {
            ack(type, MRP_ERR_BAD_VALUE, 0); break;
        }
        if (g_ptt.state == PTT_KEYED) { ack(type, MRP_ERR_BUSY, 0); break; }
        if (!radio_apply_channel(&c)) { ack(type, MRP_ERR_RADIO, 0); break; }
        g_chan = c;
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
        if (len == MRP_AUDIO_FRAME_BYTES && g_ptt.state == PTT_KEYED) {
            memcpy(g_pcm, pl, len);                 /* wire format is already s16le */
            hal_audio_tx_write(g_pcm, MRP_AUDIO_FRAME_SAMPLES);
        }
        break;
    case MRP_KEEPALIVE:
    case MRP_GET_STATUS:
        break;                                       /* status is pushed by the 1 Hz task */
    case MRP_DATA_TX:
        ack(type, MRP_ERR_UNSUPPORTED, 0);           /* AFSK data path: phase 2 */
        break;
    default:
        ack(type, MRP_ERR_UNSUPPORTED, 0);
    }
}

/* ----------------------------------------------------------- PD glue */
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

/* Called by the port layer when the PD controller reports an event. */
void app_pd_event(const pd_event_t *ev)
{
    pd_apply(pd_policy_event(&g_pd, ev, hal_millis()));
}

/* ----------------------------------------------------------- 1 Hz task */
static void status_task(uint32_t now)
{
    hal_power_status_t ps;
    hal_power_read(&ps);
    int8_t pa_t = hal_pa_temp_c();
    int8_t hot = pa_t > ps.temp_c ? pa_t : ps.temp_c;

    /* inhibit transmit when the PA is too hot or the cells are nearly empty */
    ptt_guard_set_inhibit(&g_ptt, hot > 70 || ps.soc_pct < 5 || !g_radio_ok, now);
    pd_apply(pd_policy_tick(&g_pd, now, ps.soc_pct));

    mrp_status_t s = {
        .flags = (uint16_t)((g_ptt.state == PTT_KEYED ? MRP_ST_PTT : 0) |
                            (hal_radio_squelch_open() ? MRP_ST_SQ_OPEN : 0) |
                            (g_ptt.inhibit ? MRP_ST_TX_INHIBIT : 0) |
                            (ps.charging ? MRP_ST_CHARGING : 0) |
                            (ps.sourcing ? MRP_ST_SOURCING : 0) |
                            (ps.fault ? MRP_ST_FAULT : 0) |
                            (g_radio_ok ? MRP_ST_RADIO_OK : 0) |
                            (now - g_last_host_ms < 3000 ? MRP_ST_HOST_ALIVE : 0)),
        .rssi_dbm = -127, .channel = g_chan.channel,
        .batt_mv = ps.batt_mv, .batt_ma = ps.batt_ma, .soc_pct = ps.soc_pct, .temp_c = hot,
        .vbus_mv = ps.vbus_mv, .vbus_ma = ps.vbus_ma,
        .pd_role = (uint8_t)((g_pd.power_role == PD_POWER_SOURCE ? MRP_PD_SOURCE : 0) |
                             (g_pd.data_role == PD_DATA_DFP ? MRP_PD_DFP : 0) |
                             (g_pd.partner_pd ? MRP_PD_CONTRACT : 0) |
                             (g_pd.partner_drp ? MRP_PD_PARTNER_DRP : 0)),
        .pd_mv = ps.vbus_mv, .pd_ma = (uint16_t)(ps.vbus_ma < 0 ? -ps.vbus_ma : ps.vbus_ma),
        .tx_remaining_s = ptt_guard_tx_remaining_s(&g_ptt, now),
    };
    if (g_ptt.state != PTT_KEYED) {                 /* RSSI query only while receiving */
        char cmd[16], line[32]; sa818_resp_t r;
        int n = sa818_cmd_rssi(cmd, sizeof cmd);
        hal_radio_uart_write(cmd, (size_t)n);
        if (hal_radio_uart_read_line(line, sizeof line, 50) && sa818_parse_response(line, &r) &&
            r.kind == SA818_RESP_RSSI)
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
    bool keyed = false;

    mrp_parser_init(&g_parser, on_frame, NULL);
    pd_policy_init(&g_pd, NULL);
    ptt_guard_init(&g_ptt, NULL);
    g_radio_ok = radio_init();
    hal_log("radio %s\n", g_radio_ok ? "ready" : "NOT RESPONDING");

    for (;;) {
        uint32_t now = hal_millis();
        hal_watchdog_kick();

        /* host -> device */
        size_t n = hal_usb_read(rx, sizeof rx);
        if (n) mrp_parser_feed(&g_parser, rx, n);

        /* local PTT button (standalone use, phone optional) */
        static bool btn_prev;
        bool btn = hal_button_ptt();
        if (btn != btn_prev) {
            ptt_guard_set_channel_busy(&g_ptt, hal_radio_squelch_open());
            ptt_guard_request(&g_ptt, PTT_SRC_LOCAL, btn, now);
            btn_prev = btn;
        }

        /* keying with guard; switch audio paths with it */
        bool key_now = ptt_guard_tick(&g_ptt, now);
        if (key_now != keyed) {
            keyed = key_now;
            hal_audio_tx_enable(keyed);
            hal_radio_ptt(keyed);
            hal_audio_local_path(!keyed && !hal_usb_connected(), keyed && g_ptt.source == PTT_SRC_LOCAL);
        }

        /* receiver audio -> host, 20 ms frames */
        if (!keyed && hal_usb_connected()) {
            while (hal_audio_rx_read(g_pcm, MRP_AUDIO_FRAME_SAMPLES) == MRP_AUDIO_FRAME_SAMPLES)
                send(MRP_AUDIO_RX, g_pcm, MRP_AUDIO_FRAME_BYTES);
        }

        if ((int32_t)(now - next_status) >= 0) {
            status_task(now);
            next_status = now + 1000;
        }
        hal_delay_ms(1);
    }
}
