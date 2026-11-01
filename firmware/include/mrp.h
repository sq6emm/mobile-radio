/*
 * MRP - Mobile Radio Protocol (v1)
 *
 * Binary framing used between the phone (USB host) and the battery-pack radio
 * over the vendor-specific USB bulk interface (and, identically, over a UART
 * for bench debugging).  Little-endian throughout.
 *
 *   +------+------+-----+--------+-----+---------+---------+
 *   | SOF  | type | seq | len LE | HCS | payload | CRC16 LE|
 *   | 0xA5 | u8   | u8  | u16    | u8  | len B   | u16     |
 *   +------+------+-----+--------+-----+---------+---------+
 *
 * HCS = type ^ seq ^ len_lo ^ len_hi lets the parser reject a false start
 * byte before it commits to waiting for 'len' bytes.
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over type..payload (i.e.
 * everything between SOF and the CRC, HCS included).
 * Host -> device types are 0x01..0x7E, device -> host 0x80..0xFE.
 */
#ifndef MRP_H
#define MRP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MRP_PROTO_VERSION 1
#define MRP_SOF           0xA5
#define MRP_HDR_LEN       6
#define MRP_CRC_LEN       2
#define MRP_CRC_INIT      0xFFFF
#define MRP_MAX_PAYLOAD   1024
#define MRP_MAX_FRAME     (MRP_HDR_LEN + MRP_MAX_PAYLOAD + MRP_CRC_LEN)

/* Audio format on the wire: 16 kHz, mono, signed 16-bit, 20 ms per frame. */
#define MRP_AUDIO_RATE_HZ        16000
#define MRP_AUDIO_FRAME_SAMPLES  320
#define MRP_AUDIO_FRAME_BYTES    (MRP_AUDIO_FRAME_SAMPLES * 2)

enum mrp_type {
    /* host -> device */
    MRP_GET_INFO    = 0x01,
    MRP_SET_CHANNEL = 0x02,
    MRP_PTT         = 0x03,
    MRP_SET_AUDIO   = 0x04,
    MRP_SET_POWER   = 0x05,
    MRP_DATA_TX     = 0x06,   /* short text/data packet to send over the air (AFSK) */
    MRP_GET_STATUS  = 0x07,
    MRP_KEEPALIVE   = 0x08,   /* must arrive at least every 2 s while PTT is held */
    MRP_AUDIO_TX    = 0x10,   /* PCM from phone mic -> transmitter */
    /* device -> host */
    MRP_NAK         = 0x7F,
    MRP_INFO        = 0x81,
    MRP_ACK         = 0x82,
    MRP_AUDIO_RX    = 0x90,   /* PCM from receiver -> phone speaker */
    MRP_STATUS      = 0x91,
    MRP_DATA_RX     = 0x92,
};

/* ----- payloads (wire sizes given; packed/unpacked by helpers below) ----- */

#define MRP_SET_CHANNEL_LEN 7
typedef struct {
    uint8_t channel;    /* 1..16 (PMR446) */
    uint8_t bandwidth;  /* 0 = 12.5 kHz (only legal value for PMR446) */
    uint8_t ctcss_tx;   /* 0 = none, 1..38 = CTCSS tone index */
    uint8_t ctcss_rx;
    uint8_t squelch;    /* 0 = open .. 8 = tight */
    uint8_t power;      /* 0 = low, 1 = high (firmware caps it to the ERP limit) */
    uint8_t flags;      /* bit0 busy-channel lockout */
} mrp_set_channel_t;
#define MRP_CHAN_FLAG_BCLO 0x01

#define MRP_PTT_LEN 3
typedef struct {
    uint8_t  state;     /* 0 = release, 1 = key */
    uint16_t ttl_ms;    /* auto-release if no KEEPALIVE/PTT within this time (0 = default) */
} mrp_ptt_t;

#define MRP_SET_AUDIO_LEN 3
typedef struct {
    uint8_t volume;       /* 1..8 local speaker volume */
    uint8_t mic_gain;     /* 0..15 */
    uint8_t filter_flags; /* bit0 bypass pre/de-emphasis, bit1 bypass 300 Hz HPF, bit2 bypass 3 kHz LPF */
} mrp_set_audio_t;

#define MRP_SET_POWER_LEN 3
typedef struct {
    uint8_t flags;           /* bit0 allow sourcing to phone, bit1 allow charging from port */
    uint8_t max_source_w;    /* 0 = default (18) */
    uint8_t reserve_soc_pct; /* stop sourcing below this state of charge (default 20) */
} mrp_set_power_t;
#define MRP_PWR_FLAG_SOURCE 0x01
#define MRP_PWR_FLAG_CHARGE 0x02

#define MRP_ACK_LEN 4
typedef struct {
    uint8_t  req_type;  /* type being acknowledged */
    uint8_t  result;    /* 0 = ok, else mrp_result */
    uint16_t detail;
} mrp_ack_t;

enum mrp_result {
    MRP_OK = 0,
    MRP_ERR_BAD_LEN = 1,
    MRP_ERR_BAD_VALUE = 2,
    MRP_ERR_RADIO = 3,
    MRP_ERR_BUSY = 4,        /* e.g. busy-channel lockout refused PTT */
    MRP_ERR_INHIBITED = 5,   /* TOT, over-temperature, low battery */
    MRP_ERR_UNSUPPORTED = 6,
};

#define MRP_INFO_LEN 26
typedef struct {
    uint8_t  proto;
    uint8_t  fw_major, fw_minor, hw_rev;
    uint32_t caps;           /* MRP_CAP_* */
    char     serial[16];     /* NUL padded */
    uint8_t  region;         /* 0 = EU PMR446 */
    uint8_t  n_channels;
} mrp_info_t;
#define MRP_CAP_VOICE   (1u << 0)
#define MRP_CAP_DATA    (1u << 1)
#define MRP_CAP_PD_SRC  (1u << 2)
#define MRP_CAP_PD_SNK  (1u << 3)
#define MRP_CAP_UAC     (1u << 4)   /* can re-enumerate with a USB Audio Class profile */

#define MRP_STATUS_LEN 21
typedef struct {
    uint16_t flags;          /* MRP_ST_* */
    int8_t   rssi_dbm;
    uint8_t  channel;
    uint16_t batt_mv;
    int16_t  batt_ma;        /* + = charging, - = discharging */
    uint8_t  soc_pct;
    int8_t   temp_c;         /* hottest of PA / cells */
    uint16_t vbus_mv;
    int16_t  vbus_ma;        /* + = sourcing to phone, - = sinking */
    uint8_t  pd_role;        /* MRP_PD_* */
    uint16_t pd_mv, pd_ma;   /* negotiated contract */
    uint16_t tx_remaining_s; /* time left before transmit time-out */
} mrp_status_t;
#define MRP_ST_PTT        (1u << 0)
#define MRP_ST_SQ_OPEN    (1u << 1)
#define MRP_ST_TX_INHIBIT (1u << 2)
#define MRP_ST_CHARGING   (1u << 3)
#define MRP_ST_SOURCING   (1u << 4)
#define MRP_ST_FAULT      (1u << 5)
#define MRP_ST_RADIO_OK   (1u << 6)
#define MRP_ST_HOST_ALIVE (1u << 7)
#define MRP_PD_SOURCE     (1u << 0)
#define MRP_PD_DFP        (1u << 1)
#define MRP_PD_CONTRACT   (1u << 2)
#define MRP_PD_PARTNER_DRP (1u << 3)

/* ----- CRC and framing ----- */
uint16_t mrp_crc16(const uint8_t *data, size_t len, uint16_t crc);

/* Build a complete frame into out[]. Returns frame length or 0 on error. */
size_t mrp_encode(uint8_t *out, size_t cap, uint8_t type, uint8_t seq,
                  const void *payload, uint16_t len);

typedef void (*mrp_frame_cb)(void *user, uint8_t type, uint8_t seq,
                             const uint8_t *payload, uint16_t len);

typedef struct {
    uint8_t  buf[MRP_MAX_FRAME];
    size_t   pos;
    mrp_frame_cb cb;
    void    *user;
    uint32_t frames_ok, crc_errors, resyncs;
} mrp_parser_t;

void mrp_parser_init(mrp_parser_t *p, mrp_frame_cb cb, void *user);
void mrp_parser_feed(mrp_parser_t *p, const uint8_t *data, size_t n);

/* ----- payload (un)packing: return wire length, or 0 if the input is invalid ----- */
size_t mrp_pack_set_channel(const mrp_set_channel_t *m, uint8_t *buf);
size_t mrp_pack_ptt(const mrp_ptt_t *m, uint8_t *buf);
size_t mrp_pack_set_audio(const mrp_set_audio_t *m, uint8_t *buf);
size_t mrp_pack_set_power(const mrp_set_power_t *m, uint8_t *buf);
size_t mrp_pack_ack(const mrp_ack_t *m, uint8_t *buf);
size_t mrp_pack_info(const mrp_info_t *m, uint8_t *buf);
size_t mrp_pack_status(const mrp_status_t *m, uint8_t *buf);

bool mrp_unpack_set_channel(const uint8_t *buf, uint16_t len, mrp_set_channel_t *m);
bool mrp_unpack_ptt(const uint8_t *buf, uint16_t len, mrp_ptt_t *m);
bool mrp_unpack_set_audio(const uint8_t *buf, uint16_t len, mrp_set_audio_t *m);
bool mrp_unpack_set_power(const uint8_t *buf, uint16_t len, mrp_set_power_t *m);
bool mrp_unpack_ack(const uint8_t *buf, uint16_t len, mrp_ack_t *m);
bool mrp_unpack_info(const uint8_t *buf, uint16_t len, mrp_info_t *m);
bool mrp_unpack_status(const uint8_t *buf, uint16_t len, mrp_status_t *m);

#ifdef __cplusplus
}
#endif
#endif /* MRP_H */
