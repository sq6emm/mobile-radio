#include "arbiter.h"
#include <string.h>

void arbiter_init(arbiter_t *a)
{
    memset(a, 0, sizeof(*a));
}

void arbiter_set_rx_pref(arbiter_t *a, rx_pref_t pref)
{
    a->rx_pref = pref;
}

static int prio(tx_kind_t k)
{
    switch (k) {
    case TX_FM_VOICE: return 3;
    case TX_FM_DV:    return 3;
    case TX_FM_DATA:  return 2;
    case TX_LORA433:  return 1;
    default:          return 0;
    }
}

arb_result_t arbiter_request(arbiter_t *a, tx_kind_t kind, uint32_t now_ms, tx_kind_t *preempted)
{
    if (preempted) *preempted = TX_IDLE;
    if (kind == TX_LORA868) {
        if (a->lora868_tx) return ARB_BUSY;
        a->lora868_tx = true;
        return ARB_GRANTED;
    }
    if (kind == TX_IDLE) return ARB_BUSY;
    if (a->shared_owner == TX_IDLE) {
        a->shared_owner = kind;
        a->owner_since_ms = now_ms;
        return ARB_GRANTED;
    }
    if (a->shared_owner == kind) return ARB_GRANTED;      /* already ours */
    if (prio(kind) > prio(a->shared_owner)) {
        if (preempted) *preempted = a->shared_owner;
        a->shared_owner = kind;
        a->owner_since_ms = now_ms;
        return ARB_PREEMPTED;
    }
    return ARB_BUSY;
}

void arbiter_release(arbiter_t *a, tx_kind_t kind)
{
    if (kind == TX_LORA868) { a->lora868_tx = false; return; }
    if (a->shared_owner == kind) a->shared_owner = TX_IDLE;
}

bool arbiter_fm_tx_active(const arbiter_t *a)
{
    return a->shared_owner == TX_FM_VOICE || a->shared_owner == TX_FM_DV || a->shared_owner == TX_FM_DATA;
}

ant_pos_t arbiter_antenna(const arbiter_t *a)
{
    if (a->shared_owner == TX_LORA433) return ANT_TO_LORA433;
    if (arbiter_fm_tx_active(a)) return ANT_TO_FM;
    return a->rx_pref == RX_PREF_LORA433 ? ANT_TO_LORA433 : ANT_TO_FM;
}

bool arbiter_lora433_may_listen(const arbiter_t *a)
{
    return arbiter_antenna(a) == ANT_TO_LORA433 && a->shared_owner == TX_IDLE;
}

bool arbiter_fm_may_receive(const arbiter_t *a)
{
    return arbiter_antenna(a) == ANT_TO_FM && a->shared_owner == TX_IDLE;
}

tx_kind_t arbiter_owner(const arbiter_t *a)
{
    return a->shared_owner;
}
