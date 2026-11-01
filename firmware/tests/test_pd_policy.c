#include "pd_policy.h"
#include "test.h"

static pd_action_t attach(pd_policy_t *p, pd_power_role_t pr, pd_data_role_t dr, bool pd, bool drp, uint32_t t)
{
    pd_event_t ev = { .kind = PD_EV_ATTACHED, .power_role = pr, .data_role = dr, .partner_pd = pd, .partner_drp = drp };
    return pd_policy_event(p, &ev, t);
}
static pd_action_t swap_result(pd_policy_t *p, pd_event_kind_t k, bool ok, uint32_t t)
{
    pd_event_t ev = { .kind = k, .accepted = ok };
    return pd_policy_event(p, &ev, t);
}

int main(void)
{
    pd_policy_t p;

    /* 1. Phone (Try.SNK) wins as source/DFP; we come up sink/UFP -> ask for PR_Swap, then source. */
    pd_policy_init(&p, NULL);
    pd_action_t a = attach(&p, PD_POWER_SINK, PD_DATA_UFP, true, true, 1000);
    CHECK_EQ(a.kind, PD_ACT_REQUEST_PR_SWAP);
    a = swap_result(&p, PD_EV_PR_SWAP_RESULT, true, 1200);
    CHECK_EQ(a.kind, PD_ACT_ENABLE_SOURCE);
    CHECK_EQ(a.arg, 18);
    CHECK(pd_policy_goal_reached(&p));
    CHECK_EQ(pd_policy_tick(&p, 2000, 90).kind, PD_ACT_NONE);

    /* 2. We win as source/DFP (Try.SRC) -> ask for DR_Swap to become the USB device. */
    pd_policy_init(&p, NULL);
    a = attach(&p, PD_POWER_SOURCE, PD_DATA_DFP, true, true, 0);
    CHECK_EQ(a.kind, PD_ACT_REQUEST_DR_SWAP);
    a = swap_result(&p, PD_EV_DR_SWAP_RESULT, true, 100);
    CHECK_EQ(a.kind, PD_ACT_ENABLE_SOURCE);
    CHECK(pd_policy_goal_reached(&p));

    /* 3. Phone rejects PR_Swap: back off, retry after 30 s, give up after 3 attempts, keep trickle sink. */
    pd_policy_init(&p, NULL);
    a = attach(&p, PD_POWER_SINK, PD_DATA_UFP, true, true, 0);
    CHECK_EQ(a.kind, PD_ACT_REQUEST_PR_SWAP);
    CHECK_EQ(swap_result(&p, PD_EV_PR_SWAP_RESULT, false, 10).kind, PD_ACT_NONE);
    CHECK_EQ(pd_policy_tick(&p, 1000, 90).kind, PD_ACT_NONE);
    CHECK_EQ(pd_policy_tick(&p, 30010, 90).kind, PD_ACT_REQUEST_PR_SWAP);
    swap_result(&p, PD_EV_PR_SWAP_RESULT, false, 30020);
    CHECK_EQ(pd_policy_tick(&p, 60030, 90).kind, PD_ACT_REQUEST_PR_SWAP);
    swap_result(&p, PD_EV_PR_SWAP_RESULT, false, 60040);
    CHECK_EQ(pd_policy_tick(&p, 90050, 90).kind, PD_ACT_NONE);
    CHECK_EQ(pd_policy_tick(&p, 900000, 90).kind, PD_ACT_NONE);
    CHECK(!pd_policy_goal_reached(&p));

    /* 4. Wall charger (not DRP): just sink at full current, never swap. */
    pd_policy_init(&p, NULL);
    a = attach(&p, PD_POWER_SINK, PD_DATA_UFP, true, false, 0);
    CHECK_EQ(a.kind, PD_ACT_SET_SINK_LIMIT_MA);
    CHECK_EQ(a.arg, 3000);
    CHECK_EQ(pd_policy_tick(&p, 5000, 50).kind, PD_ACT_NONE);

    /* 5. Legacy 5 V host without PD: nothing to negotiate. */
    pd_policy_init(&p, NULL);
    a = attach(&p, PD_POWER_SINK, PD_DATA_UFP, false, false, 0);
    CHECK_EQ(a.kind, PD_ACT_SET_SINK_LIMIT_MA);
    CHECK_EQ(pd_policy_tick(&p, 5000, 50).kind, PD_ACT_NONE);

    /* 6. Sourcing, battery drops under reserve -> hand power back (PR_Swap), trickle sink; resume at 30 %. */
    pd_policy_init(&p, NULL);
    attach(&p, PD_POWER_SINK, PD_DATA_UFP, true, true, 0);
    swap_result(&p, PD_EV_PR_SWAP_RESULT, true, 10);
    CHECK(pd_policy_goal_reached(&p));
    CHECK_EQ(pd_policy_tick(&p, 1000, 25).kind, PD_ACT_NONE);
    a = pd_policy_tick(&p, 2000, 19);
    CHECK_EQ(a.kind, PD_ACT_REQUEST_PR_SWAP);
    a = swap_result(&p, PD_EV_PR_SWAP_RESULT, true, 2100);
    CHECK_EQ(a.kind, PD_ACT_SET_SINK_LIMIT_MA);
    CHECK_EQ(a.arg, 100);
    CHECK_EQ(p.power_role, PD_POWER_SINK);
    CHECK_EQ(pd_policy_tick(&p, 3000, 25).kind, PD_ACT_NONE);       /* hysteresis */
    CHECK_EQ(pd_policy_tick(&p, 4000, 31).kind, PD_ACT_REQUEST_PR_SWAP);

    /* 7. Phone asks to become source while we still want to charge it -> reject; after low battery -> accept. */
    pd_policy_init(&p, NULL);
    attach(&p, PD_POWER_SOURCE, PD_DATA_UFP, true, true, 0);
    pd_event_t req = { .kind = PD_EV_PARTNER_REQUESTED_PR_SWAP };
    CHECK_EQ(pd_policy_event(&p, &req, 10).kind, PD_ACT_REJECT_PR_SWAP);
    p.cfg.allow_source = false;
    CHECK_EQ(pd_policy_event(&p, &req, 20).kind, PD_ACT_ACCEPT_PR_SWAP);

    /* 8. Detach disables the source and clears pending swaps. */
    pd_event_t det = { .kind = PD_EV_DETACHED };
    CHECK_EQ(pd_policy_event(&p, &det, 30).kind, PD_ACT_DISABLE_SOURCE);
    CHECK(!p.attached);
    CHECK_EQ(pd_policy_tick(&p, 40, 90).kind, PD_ACT_NONE);

    /* 9. User disabled sourcing: with a phone we stay sink and only trickle. */
    pd_policy_init(&p, NULL);
    p.cfg.allow_source = false;
    a = attach(&p, PD_POWER_SINK, PD_DATA_UFP, true, true, 0);
    CHECK_EQ(a.kind, PD_ACT_SET_SINK_LIMIT_MA);
    CHECK_EQ(a.arg, 3000);  /* allow_sink still true: let the phone charge us if it wants */
    TEST_END();
}
