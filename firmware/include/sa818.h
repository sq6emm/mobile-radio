/*
 * SA818 / DRA818 UHF transceiver module - AT command builder and response parser.
 *
 * The module is driven over a 9600 8N1 UART. Every command ends in CRLF and is
 * answered by a single line, e.g. "+DMOSETGROUP:0". This file only builds and
 * parses strings; the port layer moves bytes. Check the datasheet revision of
 * the module you buy - the command set has small differences between vendors
 * (NiceRF SA818-U, Dorji DRA818U, SA868).
 */
#ifndef SA818_H
#define SA818_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t  bandwidth;    /* 0 = 12.5 kHz, 1 = 25 kHz */
    uint32_t tx_hz;
    uint32_t rx_hz;
    uint8_t  ctcss_tx;     /* 0..38 */
    uint8_t  ctcss_rx;     /* 0..38 */
    uint8_t  squelch;      /* 0..8 */
} sa818_group_t;

typedef enum {
    SA818_RESP_UNKNOWN = 0,
    SA818_RESP_CONNECT,
    SA818_RESP_SETGROUP,
    SA818_RESP_SETVOLUME,
    SA818_RESP_SETFILTER,
    SA818_RESP_SETTAIL,
    SA818_RESP_RSSI,
    SA818_RESP_VERSION,
    SA818_RESP_ERROR,
} sa818_resp_kind_t;

typedef struct {
    sa818_resp_kind_t kind;
    int  code;          /* ":n" status (0 = ok) for set commands */
    int  rssi_raw;      /* RSSI? answer, 0..255 */
    char version[24];
} sa818_resp_t;

int sa818_cmd_connect(char *buf, size_t cap);
int sa818_cmd_version(char *buf, size_t cap);
int sa818_cmd_setgroup(char *buf, size_t cap, const sa818_group_t *g);
int sa818_cmd_volume(char *buf, size_t cap, uint8_t level_1_to_8);
/* bypass flags: 1 = bypass (filter off), 0 = filter active, per the SA818 datasheet. */
int sa818_cmd_filter(char *buf, size_t cap, bool bypass_emphasis, bool bypass_hpf, bool bypass_lpf);
int sa818_cmd_tail(char *buf, size_t cap, bool tail_tone_on);
int sa818_cmd_rssi(char *buf, size_t cap);

/* Parse one response line (CR/LF stripped or not). Returns false if unrecognised. */
bool sa818_parse_response(const char *line, sa818_resp_t *out);

/* Raw RSSI to dBm with a linear calibration (default slope/offset are a
 * placeholder - calibrate at the RF test port against a signal generator). */
int sa818_rssi_to_dbm(int raw, float slope_db_per_lsb, float offset_dbm);

#endif
