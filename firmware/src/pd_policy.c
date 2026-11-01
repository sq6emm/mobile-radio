#include "pd_policy.h"
#include <string.h>

void pd_policy_default_cfg(pd_policy_cfg_t *cfg)
{
    cfg->allow_source = true;
    cfg->allow_sink = true;
    cfg->reserve_soc_pct = 20;
    cfg->resume_soc_pct = 30;
    cfg->max_source_w = 18;
    cfg->swap_retry_ms = 30000;
    cfg->swap_max_attempts = 3;
    cfg->trickle_sink_ma = 100;
}

void pd_policy_init(pd_policy_t *p, const pd_policy_cfg_t *cfg)
{
    memset(p, 0, sizeof(*p));
    if (cfg) p->cfg = *cfg; else pd_policy_default_cfg(&p->cfg);
    p->soc_pct = 100;
}

static pd_action_t act(pd_action_kind_t k, uint32_t arg)
{
    pd_action_t a = { k, arg };
    return a;
}

static bool want_source(const pd_policy_t *p)
{
    bool batt_ok = !p->source_paused_low_batt || p->external_power;   /* pass-through ignores the reserve */
    return p->cfg.allow_source && batt_ok && p->partner_pd && p->partner_drp;
}

/* Decide the next step towards the goal from the current roles. */
static pd_action_t next_step(pd_policy_t *p, uint32_t now_ms)
{
    if (!p->attached)
        return act(PD_ACT_NONE, 0);

    /* 1. power role */
    if (p->power_role == PD_POWER_SINK && want_source(p) && !p->pr_swap_pending &&
        p->pr_attempts < p->cfg.swap_max_attempts && (int32_t)(now_ms - p->next_swap_ms) >= 0) {
        p->pr_swap_pending = true;
        p->pr_attempts++;
        return act(PD_ACT_REQUEST_PR_SWAP, 0);
    }
    if (p->power_role == PD_POWER_SOURCE && !want_source(p) && !p->pr_swap_pending &&
        p->partner_pd && p->pr_attempts < p->cfg.swap_max_attempts &&
        (int32_t)(now_ms - p->next_swap_ms) >= 0) {
        /* hand power back: become sink, but only trickle from the phone */
        p->pr_swap_pending = true;
        p->pr_attempts++;
        return act(PD_ACT_REQUEST_PR_SWAP, 0);
    }

    /* 2. data role: we must be the device (UFP) for the phone to see the radio */
    if (p->data_role == PD_DATA_DFP && p->partner_pd && !p->dr_swap_pending &&
        p->dr_attempts < p->cfg.swap_max_attempts && (int32_t)(now_ms - p->next_swap_ms) >= 0) {
        p->dr_swap_pending = true;
        p->dr_attempts++;
        return act(PD_ACT_REQUEST_DR_SWAP, 0);
    }
    return act(PD_ACT_NONE, 0);
}

pd_action_t pd_policy_event(pd_policy_t *p, const pd_event_t *ev, uint32_t now_ms)
{
    switch (ev->kind) {
    case PD_EV_ATTACHED:
        p->attached = true;
        p->partner_pd = ev->partner_pd;
        p->partner_drp = ev->partner_drp;
        p->power_role = ev->power_role;
        p->data_role = ev->data_role;
        p->pr_swap_pending = p->dr_swap_pending = false;
        p->pr_attempts = p->dr_attempts = 0;
        p->next_swap_ms = now_ms;
        if (p->power_role == PD_POWER_SINK) {
            /* A charger or PC: take full power. A phone we intend to swap with: trickle only. */
            uint32_t ma = (ev->partner_drp && want_source(p)) ? p->cfg.trickle_sink_ma : 3000;
            if (!p->cfg.allow_sink) ma = p->cfg.trickle_sink_ma;
            pd_action_t a = next_step(p, now_ms);
            if (a.kind != PD_ACT_NONE)
                return a;
            return act(PD_ACT_SET_SINK_LIMIT_MA, ma);
        }
        return next_step(p, now_ms);

    case PD_EV_DETACHED:
        p->attached = false;
        p->pr_swap_pending = p->dr_swap_pending = false;
        return act(PD_ACT_DISABLE_SOURCE, 0);

    case PD_EV_PR_SWAP_RESULT:
        p->pr_swap_pending = false;
        if (ev->accepted) {
            p->power_role = (p->power_role == PD_POWER_SINK) ? PD_POWER_SOURCE : PD_POWER_SINK;
            p->pr_attempts = 0;
            if (p->power_role == PD_POWER_SOURCE) {
                pd_action_t a = next_step(p, now_ms); /* maybe a DR_Swap is still needed */
                return (a.kind != PD_ACT_NONE) ? a : act(PD_ACT_ENABLE_SOURCE, p->cfg.max_source_w);
            }
            return act(PD_ACT_SET_SINK_LIMIT_MA, p->cfg.trickle_sink_ma);
        }
        p->next_swap_ms = now_ms + p->cfg.swap_retry_ms;
        return act(PD_ACT_NONE, 0);

    case PD_EV_DR_SWAP_RESULT:
        p->dr_swap_pending = false;
        if (ev->accepted) {
            p->data_role = (p->data_role == PD_DATA_DFP) ? PD_DATA_UFP : PD_DATA_DFP;
            p->dr_attempts = 0;
            if (p->power_role == PD_POWER_SOURCE)
                return act(PD_ACT_ENABLE_SOURCE, p->cfg.max_source_w);
            return act(PD_ACT_NONE, 0);
        }
        p->next_swap_ms = now_ms + p->cfg.swap_retry_ms;
        return act(PD_ACT_NONE, 0);

    case PD_EV_PARTNER_REQUESTED_PR_SWAP:
        /* Phone wants to become the source (e.g. user enabled reverse charging on it).
         * Accept only when we no longer want to source; otherwise keep charging it. */
        if (p->power_role == PD_POWER_SOURCE && want_source(p))
            return act(PD_ACT_REJECT_PR_SWAP, 0);
        if (p->power_role == PD_POWER_SINK && !want_source(p))
            return act(PD_ACT_REJECT_PR_SWAP, 0);   /* we are low: do not become the source */
        p->pr_swap_pending = true;
        return act(PD_ACT_ACCEPT_PR_SWAP, 0);
    }
    return act(PD_ACT_NONE, 0);
}

void pd_policy_set_external_power(pd_policy_t *p, bool present, uint32_t now_ms)
{
    if (p->external_power == present) return;
    p->external_power = present;
    p->pr_attempts = 0;
    p->next_swap_ms = now_ms;     /* re-evaluate immediately: resume or hand back */
}

pd_action_t pd_policy_tick(pd_policy_t *p, uint32_t now_ms, uint8_t soc_pct)
{
    p->soc_pct = soc_pct;
    if (!p->source_paused_low_batt && soc_pct < p->cfg.reserve_soc_pct) {
        p->source_paused_low_batt = true;
        p->pr_attempts = 0;
        p->next_swap_ms = now_ms;
        if (p->attached && p->power_role == PD_POWER_SOURCE && !p->external_power) {
            pd_action_t a = next_step(p, now_ms);
            return (a.kind != PD_ACT_NONE) ? a : act(PD_ACT_DISABLE_SOURCE, 0);
        }
    } else if (p->source_paused_low_batt && soc_pct >= p->cfg.resume_soc_pct) {
        p->source_paused_low_batt = false;
        p->pr_attempts = 0;
        p->next_swap_ms = now_ms;
    }
    return next_step(p, now_ms);
}

bool pd_policy_goal_reached(const pd_policy_t *p)
{
    return p->attached && p->power_role == PD_POWER_SOURCE && p->data_role == PD_DATA_UFP;
}
