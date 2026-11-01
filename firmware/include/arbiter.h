/*
 * Transmit arbiter for the single FM transceiver.
 *
 * Priority: voice (phone or button) = digital voice > data (packet, beacons,
 * message retries). A higher-priority request pre-empts an active lower one
 * (the owner is told to abort). Equal priority never pre-empts.
 */
#ifndef ARBITER_H
#define ARBITER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TX_IDLE = 0,
    TX_FM_VOICE,
    TX_FM_DV,
    TX_FM_DATA,
} tx_kind_t;

typedef enum { ARB_GRANTED = 0, ARB_BUSY, ARB_PREEMPTED } arb_result_t;

typedef struct {
    tx_kind_t owner;
    uint32_t  owner_since_ms;
} arbiter_t;

void arbiter_init(arbiter_t *a);

/* Request to transmit. On ARB_PREEMPTED, *preempted names the owner that must abort now
 * and the requester holds the transmitter. ARB_BUSY: try again later. */
arb_result_t arbiter_request(arbiter_t *a, tx_kind_t kind, uint32_t now_ms, tx_kind_t *preempted);
void arbiter_release(arbiter_t *a, tx_kind_t kind);

bool      arbiter_tx_active(const arbiter_t *a);
tx_kind_t arbiter_owner(const arbiter_t *a);

#endif
