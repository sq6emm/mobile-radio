/*
 * Text message layer on top of APRS message format: ids, retries, acks,
 * duplicate suppression. Transport agnostic (FM AFSK or LoRa): the owner
 * polls msg_next_tx() and hands the resulting APRS info field to a radio.
 */
#ifndef MSG_H
#define MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MSG_OUTBOX   8
#define MSG_DEDUPE   16
#define MSG_TEXT_MAX 67
#define MSG_ADDR_MAX 9
#define MSG_RETRIES  3

typedef enum { MSG_FREE = 0, MSG_PENDING, MSG_SENT_WAIT_ACK, MSG_ACKED, MSG_FAILED } msg_state_t;

typedef struct {
    msg_state_t state;
    uint16_t id;
    char     to[MSG_ADDR_MAX + 1];
    char     text[MSG_TEXT_MAX + 1];
    uint8_t  tries;
    uint32_t next_ms;
} msg_entry_t;

typedef struct {
    char from[MSG_ADDR_MAX + 1];
    char id[6];
} msg_seen_t;

typedef struct {
    msg_entry_t outbox[MSG_OUTBOX];
    msg_seen_t  seen[MSG_DEDUPE];
    int         seen_pos;
    uint16_t    next_id;
    uint32_t    retry_base_ms;   /* 10 s default; doubles each retry */
} msg_ctx_t;

typedef struct {
    bool     valid;
    char     from[MSG_ADDR_MAX + 1];
    char     text[MSG_TEXT_MAX + 1];
    char     id[6];              /* "" = sender wants no ack */
    bool     duplicate;
} msg_rx_t;

void msg_init(msg_ctx_t *c);

/* Queue a message. Returns its id (1..) or 0 when the outbox is full / input invalid. */
uint16_t msg_send(msg_ctx_t *c, const char *to, const char *text, uint32_t now_ms);

/* If something is due, fill the APRS info field to transmit and return its id; else 0.
 * Each call that returns an id counts as one transmission attempt. */
uint16_t msg_next_tx(msg_ctx_t *c, uint32_t now_ms, char *info, size_t cap);

/* Incoming APRS info field addressed to 'me' from 'from'. Fills rx and, when an ack is
 * owed, writes it to ack_info (cap >= 32) and returns true for "ack_info must be sent". */
bool msg_on_rx(msg_ctx_t *c, const char *me, const char *from, const char *info, size_t len,
               msg_rx_t *rx, char *ack_info, size_t ack_cap);

msg_state_t msg_state(const msg_ctx_t *c, uint16_t id);
int msg_pending_count(const msg_ctx_t *c);

#endif
