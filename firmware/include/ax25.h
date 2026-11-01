/*
 * AX.25 UI frames and HDLC bit layer (for 1200 Bd AFSK packet / APRS).
 *
 *   ax25_build_ui()  -> frame bytes incl. FCS (no flags, no stuffing)
 *   hdlc_encode()    -> NRZI line bits with flags and bit stuffing, ready for the AFSK modulator
 *   hdlc_rx_feed()   -> NRZI decode, destuff, flag detect, FCS check, callback with frames
 *   ax25_parse()     -> addresses / control / PID / info
 */
#ifndef AX25_H
#define AX25_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AX25_MAX_DIGIS   8
#define AX25_MAX_FRAME   330   /* 10 addresses * 7 + 2 + 256 info + 2 FCS */
#define AX25_MAX_INFO    256
#define AX25_CTRL_UI     0x03
#define AX25_PID_NOL3    0xF0

typedef struct {
    char    call[7];   /* up to 6 chars, NUL terminated */
    uint8_t ssid;      /* 0..15 */
    bool    repeated;  /* H bit (digipeated) */
} ax25_addr_t;

typedef struct {
    ax25_addr_t dst, src;
    ax25_addr_t digi[AX25_MAX_DIGIS];
    int         ndigi;
    uint8_t     ctrl, pid;
    const uint8_t *info;
    size_t      info_len;
} ax25_frame_t;

/* CRC-16/X-25 as used by AX.25 (poly 0x1021 reflected, init 0xFFFF, xorout 0xFFFF). */
uint16_t ax25_fcs(const uint8_t *data, size_t len);

/* Fill an address from "CALL-SSID" or "CALL" text. */
bool ax25_addr_from_str(ax25_addr_t *a, const char *s);
/* Render as "CALL" or "CALL-N" (needs 10 bytes). */
void ax25_addr_to_str(const ax25_addr_t *a, char *out, size_t cap);

/* Build a UI frame (FCS appended, little-endian). Returns length or 0. */
size_t ax25_build_ui(uint8_t *out, size_t cap, const ax25_addr_t *dst, const ax25_addr_t *src,
                     const ax25_addr_t *digis, int ndigi, const uint8_t *info, size_t info_len);

/* Parse and verify FCS. info points into frame. */
bool ax25_parse(const uint8_t *frame, size_t len, ax25_frame_t *out);

/* --- HDLC bit layer. Bits are one byte each (0/1) for simplicity. --- */

/* Produce NRZI line levels: preamble flags, stuffed frame, tail flags.
 * 'level' carries the NRZI line state between consecutive calls (NULL = start at 0).
 * Returns number of bits, 0 if cap too small. */
size_t hdlc_encode(const uint8_t *frame, size_t len, int preamble_flags, int tail_flags,
                   uint8_t *bits, size_t cap, int *level);

typedef void (*hdlc_frame_cb)(void *user, const uint8_t *frame, size_t len);

typedef struct {
    uint8_t  buf[AX25_MAX_FRAME + 2];
    size_t   nbytes;
    uint32_t shift;      /* last 8 line bits for flag detection */
    uint8_t  cur;        /* byte being assembled */
    int      nbits;      /* bits in cur */
    int      ones;       /* consecutive ones for destuffing */
    int      last_level; /* NRZI state */
    bool     in_frame;
    hdlc_frame_cb cb;
    void    *user;
    uint32_t frames_ok, fcs_errors;
} hdlc_rx_t;

void hdlc_rx_init(hdlc_rx_t *r, hdlc_frame_cb cb, void *user);
void hdlc_rx_feed(hdlc_rx_t *r, int level);   /* one NRZI line level (0/1) per bit */

#endif
