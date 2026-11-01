/*
 * Transmitter keying guard.
 *
 * The PA is only keyed while ALL of these hold:
 *   - a PTT request is active (from the phone over MRP, or the local button),
 *   - the host keep-alive has not expired (a crashed/unplugged phone must not
 *     leave the transmitter on),
 *   - the transmit time-out (TOT) has not elapsed,
 *   - no inhibit is set (over-temperature, battery critically low, radio fault),
 *   - busy-channel lockout, if enabled, did not find the channel occupied.
 * Voice (HOST/LOCAL) pre-empts an active DATA transmission; DATA never
 * pre-empts anything and is refused while a voice source is keyed.
 */
#ifndef PTT_GUARD_H
#define PTT_GUARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PTT_SRC_NONE = 0,
    PTT_SRC_HOST,    /* voice from the phone (needs keep-alive) */
    PTT_SRC_LOCAL,   /* voice from the pack's button */
    PTT_SRC_DATA,    /* packet / beacon / digital voice frames from firmware; voice pre-empts it */
} ptt_source_t;

typedef enum {
    PTT_IDLE = 0,
    PTT_KEYED,
    PTT_DENIED_BUSY,
    PTT_DENIED_INHIBIT,
    PTT_RELEASED_KEEPALIVE,
    PTT_RELEASED_TOT,
    PTT_COOLDOWN,
} ptt_state_t;

typedef struct {
    uint32_t tot_ms;            /* max continuous transmission (default 180 s) */
    uint32_t keepalive_ms;      /* host must refresh within this (default 2 s) */
    uint32_t cooldown_ms;       /* forced pause after TOT (default 10 s) */
    bool     busy_lockout;
} ptt_guard_cfg_t;

typedef struct {
    ptt_guard_cfg_t cfg;
    ptt_state_t state;
    ptt_source_t source;
    uint32_t keyed_at_ms;
    uint32_t last_keepalive_ms;
    uint32_t cooldown_until_ms;
    bool inhibit;
    bool channel_busy;
} ptt_guard_t;

void ptt_guard_default_cfg(ptt_guard_cfg_t *cfg);
void ptt_guard_init(ptt_guard_t *g, const ptt_guard_cfg_t *cfg);

/* Environment inputs */
void ptt_guard_set_inhibit(ptt_guard_t *g, bool inhibit, uint32_t now_ms);
void ptt_guard_set_channel_busy(ptt_guard_t *g, bool busy);

/* Request keying (true) or release (false). Returns the resulting state. */
ptt_state_t ptt_guard_request(ptt_guard_t *g, ptt_source_t src, bool key, uint32_t now_ms);

/* Host keep-alive (any MRP frame from the host counts). */
void ptt_guard_keepalive(ptt_guard_t *g, uint32_t now_ms);

/* Periodic evaluation; returns true when the PA must be keyed right now. */
bool ptt_guard_tick(ptt_guard_t *g, uint32_t now_ms);

/* Seconds left before TOT fires (0 if not transmitting). */
uint16_t ptt_guard_tx_remaining_s(const ptt_guard_t *g, uint32_t now_ms);

#endif
