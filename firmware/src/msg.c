#include "msg.h"
#include "aprs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void msg_init(msg_ctx_t *c)
{
    memset(c, 0, sizeof(*c));
    c->next_id = 1;
    c->retry_base_ms = 10000;
}

static msg_entry_t *find(msg_ctx_t *c, uint16_t id)
{
    for (int i = 0; i < MSG_OUTBOX; i++)
        if (c->outbox[i].state != MSG_FREE && c->outbox[i].id == id) return &c->outbox[i];
    return NULL;
}

uint16_t msg_send(msg_ctx_t *c, const char *to, const char *text, uint32_t now_ms)
{
    if (!to || !text || strlen(to) < 1 || strlen(to) > MSG_ADDR_MAX || strlen(text) > MSG_TEXT_MAX || !*text)
        return 0;
    for (int i = 0; i < MSG_OUTBOX; i++) {
        msg_entry_t *e = &c->outbox[i];
        if (e->state == MSG_FREE || e->state == MSG_ACKED || e->state == MSG_FAILED) {
            memset(e, 0, sizeof(*e));
            e->state = MSG_PENDING;
            e->id = c->next_id;
            c->next_id = (uint16_t)(c->next_id % 99999 + 1);
            strncpy(e->to, to, MSG_ADDR_MAX);
            strncpy(e->text, text, MSG_TEXT_MAX);
            e->next_ms = now_ms;
            return e->id;
        }
    }
    return 0;
}

uint16_t msg_next_tx(msg_ctx_t *c, uint32_t now_ms, char *info, size_t cap)
{
    for (int i = 0; i < MSG_OUTBOX; i++) {
        msg_entry_t *e = &c->outbox[i];
        if ((e->state == MSG_PENDING || e->state == MSG_SENT_WAIT_ACK) && (int32_t)(now_ms - e->next_ms) >= 0) {
            if (e->tries >= MSG_RETRIES) { e->state = MSG_FAILED; continue; }
            char id[6];
            snprintf(id, sizeof id, "%u", e->id);
            if (!aprs_encode_message(info, cap, e->to, e->text, id)) { e->state = MSG_FAILED; continue; }
            e->tries++;
            e->state = MSG_SENT_WAIT_ACK;
            e->next_ms = now_ms + (c->retry_base_ms << (e->tries - 1));   /* 10, 20, 40 s */
            return e->id;
        }
    }
    return 0;
}

static bool seen_before(msg_ctx_t *c, const char *from, const char *id)
{
    for (int i = 0; i < MSG_DEDUPE; i++)
        if (strcmp(c->seen[i].from, from) == 0 && strcmp(c->seen[i].id, id) == 0) return true;
    strncpy(c->seen[c->seen_pos].from, from, MSG_ADDR_MAX);
    strncpy(c->seen[c->seen_pos].id, id, 5);
    c->seen_pos = (c->seen_pos + 1) % MSG_DEDUPE;
    return false;
}

bool msg_on_rx(msg_ctx_t *c, const char *me, const char *from, const char *info, size_t len,
               msg_rx_t *rx, char *ack_info, size_t ack_cap)
{
    aprs_packet_t p;
    memset(rx, 0, sizeof(*rx));
    if (!aprs_decode(info, len, &p)) return false;
    if (p.kind != APRS_KIND_MESSAGE && p.kind != APRS_KIND_ACK && p.kind != APRS_KIND_REJ) return false;
    if (strcmp(p.addressee, me) != 0) return false;        /* not for us */

    if (p.kind == APRS_KIND_ACK || p.kind == APRS_KIND_REJ) {
        msg_entry_t *e = find(c, (uint16_t)atoi(p.msg_id));
        if (e && e->state == MSG_SENT_WAIT_ACK && strcmp(e->to, from) == 0)
            e->state = (p.kind == APRS_KIND_ACK) ? MSG_ACKED : MSG_FAILED;
        return false;
    }
    rx->valid = true;
    strncpy(rx->from, from, MSG_ADDR_MAX);
    strncpy(rx->text, p.text, MSG_TEXT_MAX);
    strncpy(rx->id, p.msg_id, 5);
    if (!*p.msg_id) return false;                          /* no ack wanted */
    rx->duplicate = seen_before(c, from, p.msg_id);
    return aprs_encode_ack(ack_info, ack_cap, from, p.msg_id, false) > 0;   /* ack even duplicates */
}

msg_state_t msg_state(const msg_ctx_t *c, uint16_t id)
{
    for (int i = 0; i < MSG_OUTBOX; i++)
        if (c->outbox[i].state != MSG_FREE && c->outbox[i].id == id) return c->outbox[i].state;
    return MSG_FREE;
}

int msg_pending_count(const msg_ctx_t *c)
{
    int n = 0;
    for (int i = 0; i < MSG_OUTBOX; i++)
        if (c->outbox[i].state == MSG_PENDING || c->outbox[i].state == MSG_SENT_WAIT_ACK) n++;
    return n;
}
