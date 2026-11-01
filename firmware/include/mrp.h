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
    MRP_SET_MODE    = 0x09,   /* PMR446 / HAM */
    MRP_SET_FREQ    = 0x0A,   /* HAM mode: arbitrary frequency in band */
    MRP_SET_IDENTITY= 0x0B,   /* callsign-SSID (unlocks HAM) and nickname */
    MRP_SET_BEACON  = 0x0C,
    MRP_POSITION    = 0x0D,   /* phone GNSS fix */
    MRP_SEND_MSG    = 0x0E,
    MRP_SET_PROFILE = 0x0F,   /* 0 app, 1 sound-card (re-enumerates) */
    MRP_AUDIO_TX    = 0x10,   /* PCM from phone mic -> transmitter */
    MRP_LORA_CONFIG = 0x11,
    MRP_LORA_TX     = 0x12,   /* raw LoRa payload */
    MRP_SET_DV      = 0x13,   /* FreeDV 2400B on/off (HAM only) */
    /* device -> host */
    MRP_NAK         = 0x7F,
    MRP_INFO        = 0x81,
    MRP_ACK         = 0x82,
    MRP_AUDIO_RX    = 0x90,   /* PCM from receiver -> phone speaker */
    MRP_STATUS      = 0x91,
    MRP_DATA_RX     = 0x92,   /* raw AX.25 frame as received (for logging apps) */
    MRP_MSG_RX      = 0x93,
    MRP_MSG_STATUS  = 0x94,
    MRP_LORA_RX     = 0x95,
    MRP_APRS_RX     = 0x96,   /* decoded position/status from another station */
};

enum mrp_transport { MRP_TR_FM = 0, MRP_TR_LORA433 = 1, MRP_TR_LORA868 = 2 };
enum mrp_profile   { MRP_PROFILE_APP = 0, MRP_PROFILE_SOUNDCARD = 1 };

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

#define MRP_STATUS_LEN 29
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
    uint8_t  mode;           /* 0 PMR446, 1 HAM */
    uint8_t  profile;        /* mrp_profile */
    uint8_t  dv_flags;       /* bit0 DV enabled, bit1 DV sync */
    uint8_t  lora_flags;     /* bit0 433 listening, bit1 433 TX, bit2 868 listening, bit3 868 TX */
    uint32_t freq_hz;        /* current FM frequency */
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

#define MRP_SET_MODE_LEN 1
#define MRP_SET_PROFILE_LEN 1
#define MRP_SET_DV_LEN 1

#define MRP_SET_FREQ_LEN 9
typedef struct {
    uint32_t hz;
    uint8_t  squelch, power, ctcss_tx, ctcss_rx, bandwidth;
} mrp_set_freq_t;

#define MRP_SET_IDENTITY_LEN 17
typedef struct {
    char    callsign[8];     /* NUL padded, "" = keep */
    uint8_t ssid;
    char    nickname[8];     /* NUL padded, "" = keep */
} mrp_set_identity_t;

#define MRP_SET_BEACON_LEN 55
typedef struct {
    uint8_t  enabled, smart;
    uint16_t interval_s;
    char     sym_table, sym_code;
    uint8_t  transport;      /* mrp_transport, 3 = FM + LoRa433 */
    char     path[16];
    char     comment[32];
} mrp_set_beacon_t;

#define MRP_POSITION_LEN 15
typedef struct {
    int32_t  lat_e7, lon_e7;
    int16_t  alt_m;
    uint16_t speed_kmh;
    int16_t  course_deg;     /* -1 = unknown */
    uint8_t  valid;
} mrp_position_t;

#define MRP_SEND_MSG_HDR 11      /* transport u8, to char[10], then text */
#define MRP_MSG_RX_HDR   17      /* transport u8, from char[10], id char[6], then text */
#define MRP_MSG_STATUS_LEN 3
typedef struct { uint16_t id; uint8_t state; } mrp_msg_status_t;

#define MRP_LORA_CONFIG_LEN 13
typedef struct {
    uint8_t  radio;          /* 0 = 433, 1 = 868 */
    uint8_t  preset;         /* 0 custom, 1 aprs433, 2 meshtastic868 phy, 3 pack433, 4 pack868 */
    uint32_t freq_hz;
    uint8_t  sf, bw, cr;
    int8_t   power_dbm;
    uint16_t sync_word;
    uint8_t  rx_pref;        /* 0 FM, 1 LoRa433 (antenna preference when idle) */
} mrp_lora_config_t;

#define MRP_LORA_RX_HDR 4        /* radio u8, rssi i16, snr i8, then payload */
#define MRP_APRS_RX_HDR 24       /* transport u8, src char[10], kind u8, lat_e7 i32, lon_e7 i32, course i16, speed u16, then text */

size_t mrp_pack_set_freq(const mrp_set_freq_t *m, uint8_t *buf);
size_t mrp_pack_set_identity(const mrp_set_identity_t *m, uint8_t *buf);
size_t mrp_pack_set_beacon(const mrp_set_beacon_t *m, uint8_t *buf);
size_t mrp_pack_position(const mrp_position_t *m, uint8_t *buf);
size_t mrp_pack_msg_status(const mrp_msg_status_t *m, uint8_t *buf);
size_t mrp_pack_lora_config(const mrp_lora_config_t *m, uint8_t *buf);
bool mrp_unpack_set_freq(const uint8_t *buf, uint16_t len, mrp_set_freq_t *m);
bool mrp_unpack_set_identity(const uint8_t *buf, uint16_t len, mrp_set_identity_t *m);
bool mrp_unpack_set_beacon(const uint8_t *buf, uint16_t len, mrp_set_beacon_t *m);
bool mrp_unpack_position(const uint8_t *buf, uint16_t len, mrp_position_t *m);
bool mrp_unpack_msg_status(const uint8_t *buf, uint16_t len, mrp_msg_status_t *m);
bool mrp_unpack_lora_config(const uint8_t *buf, uint16_t len, mrp_lora_config_t *m);

/* variable-length helpers; return total length or 0 */
size_t mrp_pack_send_msg(uint8_t *buf, size_t cap, uint8_t transport, const char *to, const char *text);
bool   mrp_unpack_send_msg(const uint8_t *buf, uint16_t len, uint8_t *transport, char to[11], char text[68]);
size_t mrp_pack_msg_rx(uint8_t *buf, size_t cap, uint8_t transport, const char *from, const char *id, const char *text);
size_t mrp_pack_lora_rx(uint8_t *buf, size_t cap, uint8_t radio, int16_t rssi, int8_t snr, const uint8_t *pl, size_t n);
size_t mrp_pack_aprs_rx(uint8_t *buf, size_t cap, uint8_t transport, const char *src, uint8_t kind,
                        int32_t lat_e7, int32_t lon_e7, int16_t course, uint16_t speed, const char *text);

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
