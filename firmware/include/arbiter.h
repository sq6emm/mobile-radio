/*
 * Transmit / antenna arbiter.
 *
 * Shared resources:
 *   - the SA818 (one FM transmission at a time: voice, digital voice or data),
 *   - the 439 MHz helix, shared between the SA818 and the 433 MHz LoRa module
 *     through an RF switch,
 *   - the 868 MHz LoRa module has its own antenna and is independent.
 *
 * Priority on the shared path: FM voice = FM digital voice > FM data > LoRa433.
 * A higher-priority request pre-empts an active lower one (the owner is told
 * to abort). Equal priority never pre-empts.
 *
 * Receive side: when idle, the antenna goes to the preferred receiver (FM by
 * default). LoRa433 can only listen while it owns the antenna, which is why a
 * continuous LoRa-APRS/mesh receive needs RX_PREF_LORA433 (FM receive muted).
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
    TX_LORA433,
    TX_LORA868,
} tx_kind_t;

typedef enum { RX_PREF_FM = 0, RX_PREF_LORA433 = 1 } rx_pref_t;

typedef enum { ARB_GRANTED = 0, ARB_BUSY, ARB_PREEMPTED } arb_result_t;

typedef enum { ANT_TO_FM = 0, ANT_TO_LORA433 = 1 } ant_pos_t;

typedef struct {
    tx_kind_t shared_owner;    /* TX_IDLE, FM_*, or LORA433 */
    bool      lora868_tx;
    rx_pref_t rx_pref;
    uint32_t  owner_since_ms;
} arbiter_t;

void arbiter_init(arbiter_t *a);
void arbiter_set_rx_pref(arbiter_t *a, rx_pref_t pref);

/* Request to transmit. On ARB_PREEMPTED, *preempted names the owner that must abort now
 * and the requester holds the path. ARB_BUSY: try again later. */
arb_result_t arbiter_request(arbiter_t *a, tx_kind_t kind, uint32_t now_ms, tx_kind_t *preempted);
void arbiter_release(arbiter_t *a, tx_kind_t kind);

bool      arbiter_fm_tx_active(const arbiter_t *a);
ant_pos_t arbiter_antenna(const arbiter_t *a);          /* where the RF switch must point now */
bool      arbiter_lora433_may_listen(const arbiter_t *a);
bool      arbiter_fm_may_receive(const arbiter_t *a);
tx_kind_t arbiter_owner(const arbiter_t *a);

#endif
