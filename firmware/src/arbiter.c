#include "arbiter.h"
#include <string.h>

void arbiter_init(arbiter_t *a)
{
    memset(a, 0, sizeof(*a));
}

static int prio(tx_kind_t k)
{
    switch (k) {
    case TX_FM_VOICE: return 3;
    case TX_FM_DV:    return 3;
    case TX_FM_DATA:  return 2;
    default:          return 0;
    }
}

arb_result_t arbiter_request(arbiter_t *a, tx_kind_t kind, uint32_t now_ms, tx_kind_t *preempted)
{
    if (preempted) *preempted = TX_IDLE;
    if (kind == TX_IDLE) return ARB_BUSY;
    if (a->owner == TX_IDLE) {
        a->owner = kind;
        a->owner_since_ms = now_ms;
        return ARB_GRANTED;
    }
    if (a->owner == kind) return ARB_GRANTED;
    if (prio(kind) > prio(a->owner)) {
        if (preempted) *preempted = a->owner;
        a->owner = kind;
        a->owner_since_ms = now_ms;
        return ARB_PREEMPTED;
    }
    return ARB_BUSY;
}

void arbiter_release(arbiter_t *a, tx_kind_t kind)
{
    if (a->owner == kind) a->owner = TX_IDLE;
}

bool arbiter_tx_active(const arbiter_t *a)
{
    return a->owner != TX_IDLE;
}

tx_kind_t arbiter_owner(const arbiter_t *a)
{
    return a->owner;
}
