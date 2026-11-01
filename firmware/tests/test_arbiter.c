#include "arbiter.h"
#include "test.h"

int main(void)
{
    arbiter_t a; arbiter_init(&a);
    tx_kind_t pre;
    CHECK(!arbiter_tx_active(&a));
    CHECK_EQ(arbiter_request(&a, TX_FM_DATA, 10, &pre), ARB_GRANTED);
    CHECK(arbiter_tx_active(&a));
    CHECK_EQ(arbiter_request(&a, TX_FM_DATA, 11, &pre), ARB_GRANTED);        /* re-request by owner */
    CHECK_EQ(arbiter_request(&a, TX_FM_VOICE, 30, &pre), ARB_PREEMPTED);     /* voice pre-empts data */
    CHECK_EQ(pre, TX_FM_DATA);
    CHECK_EQ(arbiter_request(&a, TX_FM_DV, 40, &pre), ARB_BUSY);             /* equal priority: no pre-emption */
    CHECK_EQ(arbiter_request(&a, TX_FM_DATA, 41, &pre), ARB_BUSY);           /* data waits for voice */
    arbiter_release(&a, TX_FM_DATA);                                         /* stale release ignored */
    CHECK_EQ(arbiter_owner(&a), TX_FM_VOICE);
    arbiter_release(&a, TX_FM_VOICE);
    CHECK_EQ(arbiter_owner(&a), TX_IDLE);
    CHECK_EQ(arbiter_request(&a, TX_FM_DV, 50, &pre), ARB_GRANTED);
    CHECK_EQ(arbiter_request(&a, TX_FM_VOICE, 51, &pre), ARB_BUSY);
    arbiter_release(&a, TX_FM_DV);
    CHECK_EQ(arbiter_request(&a, TX_IDLE, 80, &pre), ARB_BUSY);
    TEST_END();
}
