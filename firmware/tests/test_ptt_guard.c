#include "ptt_guard.h"
#include "test.h"

int main(void)
{
    ptt_guard_t g;
    ptt_guard_init(&g, NULL);

    /* key from host, keep alive, release */
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, true, 1000), PTT_KEYED);
    CHECK(ptt_guard_tick(&g, 1500));
    ptt_guard_keepalive(&g, 2500);
    CHECK(ptt_guard_tick(&g, 4000));
    CHECK_EQ(ptt_guard_tx_remaining_s(&g, 4000), 177);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, false, 4100), PTT_IDLE);
    CHECK(!ptt_guard_tick(&g, 4200));

    /* host vanishes while keyed -> released by keep-alive */
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, true, 10000), PTT_KEYED);
    CHECK(ptt_guard_tick(&g, 11999));
    CHECK(!ptt_guard_tick(&g, 12000));
    CHECK_EQ(g.state, PTT_RELEASED_KEEPALIVE);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, false, 12100), PTT_IDLE);

    /* local button does not need keep-alive, but hits TOT and then cool-down */
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, true, 20000), PTT_KEYED);
    CHECK(ptt_guard_tick(&g, 20000 + 170000));
    CHECK(!ptt_guard_tick(&g, 20000 + 180000));
    CHECK_EQ(g.state, PTT_COOLDOWN);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, true, 20000 + 185000), PTT_COOLDOWN);
    CHECK(!ptt_guard_tick(&g, 20000 + 185000));
    CHECK(!ptt_guard_tick(&g, 20000 + 190000));
    CHECK_EQ(g.state, PTT_IDLE);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, true, 20000 + 190001), PTT_KEYED);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, false, 20000 + 191000), PTT_IDLE);

    /* inhibit (thermal / low battery) denies and drops an active transmission */
    ptt_guard_set_inhibit(&g, true, 300000);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, true, 300000), PTT_DENIED_INHIBIT);
    ptt_guard_set_inhibit(&g, false, 300100);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, true, 300100), PTT_KEYED);
    ptt_guard_set_inhibit(&g, true, 300200);
    CHECK(!ptt_guard_tick(&g, 300200));
    CHECK_EQ(g.state, PTT_DENIED_INHIBIT);
    ptt_guard_set_inhibit(&g, false, 300300);
    ptt_guard_request(&g, PTT_SRC_HOST, false, 300300);

    /* busy-channel lockout */
    g.cfg.busy_lockout = true;
    ptt_guard_set_channel_busy(&g, true);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, true, 400000), PTT_DENIED_BUSY);
    ptt_guard_set_channel_busy(&g, false);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, true, 400100), PTT_KEYED);

    /* second source cannot steal an active key; release by the wrong source is ignored */
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, true, 400200), PTT_KEYED);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, false, 400300), PTT_KEYED);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_HOST, false, 400400), PTT_IDLE);

    /* data source: keyed when free, refused while voice is keyed, pre-empted by voice */
    g.cfg.busy_lockout = false;
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_DATA, true, 500000), PTT_KEYED);
    CHECK(ptt_guard_tick(&g, 500000 + 5000));                     /* no keep-alive needed for data */
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, true, 500000 + 6000), PTT_KEYED);
    CHECK_EQ(g.source, PTT_SRC_LOCAL);                             /* voice took over */
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_DATA, true, 500000 + 6100), PTT_DENIED_BUSY);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_DATA, false, 500000 + 6200), PTT_KEYED); /* data release ignored */
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_LOCAL, false, 500000 + 7000), PTT_IDLE);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_DATA, true, 500000 + 8000), PTT_KEYED);
    CHECK_EQ(ptt_guard_request(&g, PTT_SRC_DATA, false, 500000 + 9000), PTT_IDLE);
    TEST_END();
}
