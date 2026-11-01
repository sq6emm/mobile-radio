#include "arbiter.h"
#include "test.h"

int main(void)
{
    arbiter_t a; arbiter_init(&a);
    tx_kind_t pre;
    CHECK_EQ(arbiter_antenna(&a), ANT_TO_FM);
    CHECK(arbiter_fm_may_receive(&a));
    CHECK(!arbiter_lora433_may_listen(&a));

    /* LoRa433 beacon goes out when idle; voice pre-empts it */
    CHECK_EQ(arbiter_request(&a, TX_LORA433, 0, &pre), ARB_GRANTED);
    CHECK_EQ(arbiter_antenna(&a), ANT_TO_LORA433);
    CHECK(!arbiter_fm_may_receive(&a));
    CHECK_EQ(arbiter_request(&a, TX_FM_DATA, 10, &pre), ARB_PREEMPTED);
    CHECK_EQ(pre, TX_LORA433);
    CHECK_EQ(arbiter_antenna(&a), ANT_TO_FM);
    CHECK(arbiter_fm_tx_active(&a));
    CHECK_EQ(arbiter_request(&a, TX_LORA433, 20, &pre), ARB_BUSY);
    CHECK_EQ(arbiter_request(&a, TX_FM_VOICE, 30, &pre), ARB_PREEMPTED);
    CHECK_EQ(pre, TX_FM_DATA);
    CHECK_EQ(arbiter_request(&a, TX_FM_DV, 40, &pre), ARB_BUSY);      /* equal priority: no pre-emption */
    CHECK_EQ(arbiter_request(&a, TX_FM_VOICE, 50, &pre), ARB_GRANTED); /* re-request by owner */
    arbiter_release(&a, TX_FM_DATA);                                   /* stale release ignored */
    CHECK_EQ(arbiter_owner(&a), TX_FM_VOICE);
    arbiter_release(&a, TX_FM_VOICE);
    CHECK_EQ(arbiter_owner(&a), TX_IDLE);

    /* 868 is independent */
    CHECK_EQ(arbiter_request(&a, TX_LORA868, 60, &pre), ARB_GRANTED);
    CHECK_EQ(arbiter_request(&a, TX_LORA868, 61, &pre), ARB_BUSY);
    CHECK_EQ(arbiter_request(&a, TX_FM_VOICE, 62, &pre), ARB_GRANTED);
    CHECK(arbiter_fm_tx_active(&a));
    arbiter_release(&a, TX_LORA868);
    arbiter_release(&a, TX_FM_VOICE);

    /* receive preference */
    arbiter_set_rx_pref(&a, RX_PREF_LORA433);
    CHECK_EQ(arbiter_antenna(&a), ANT_TO_LORA433);
    CHECK(arbiter_lora433_may_listen(&a));
    CHECK(!arbiter_fm_may_receive(&a));
    CHECK_EQ(arbiter_request(&a, TX_FM_VOICE, 70, &pre), ARB_GRANTED); /* voice still wins the antenna */
    CHECK_EQ(arbiter_antenna(&a), ANT_TO_FM);
    arbiter_release(&a, TX_FM_VOICE);
    CHECK_EQ(arbiter_antenna(&a), ANT_TO_LORA433);
    CHECK_EQ(arbiter_request(&a, TX_IDLE, 80, &pre), ARB_BUSY);
    TEST_END();
}
