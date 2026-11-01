#include "msg.h"
#include "test.h"

int main(void)
{
    msg_ctx_t c; msg_init(&c);
    char info[128], ack[64]; msg_rx_t rx;

    uint16_t id = msg_send(&c, "SP6ABC", "see you at 18", 1000);
    CHECK_EQ(id, 1);
    CHECK_EQ(msg_send(&c, "", "x", 1000), 0);
    CHECK_EQ(msg_send(&c, "SP6ABC", "", 1000), 0);
    CHECK_EQ(msg_pending_count(&c), 1);
    CHECK_EQ(msg_state(&c, id), MSG_PENDING);

    /* first attempt immediately, then 10 s, 20 s, 40 s back-off, then failed */
    CHECK_EQ(msg_next_tx(&c, 1000, info, sizeof info), 1);
    CHECK_STR(info, ":SP6ABC   :see you at 18{1");
    CHECK_EQ(msg_state(&c, id), MSG_SENT_WAIT_ACK);
    CHECK_EQ(msg_next_tx(&c, 5000, info, sizeof info), 0);
    CHECK_EQ(msg_next_tx(&c, 11000, info, sizeof info), 1);
    CHECK_EQ(msg_next_tx(&c, 20000, info, sizeof info), 0);
    CHECK_EQ(msg_next_tx(&c, 31000, info, sizeof info), 1);
    CHECK_EQ(msg_next_tx(&c, 71000, info, sizeof info), 0);
    CHECK_EQ(msg_state(&c, id), MSG_FAILED);
    CHECK_EQ(msg_pending_count(&c), 0);

    /* ack path */
    id = msg_send(&c, "SP6ABC", "second", 100000);
    CHECK_EQ(id, 2);
    CHECK_EQ(msg_next_tx(&c, 100000, info, sizeof info), 2);
    const char *a = ":SQ6EMM-7 :ack2";
    CHECK(!msg_on_rx(&c, "SQ6EMM-7", "SP6ABC", a, strlen(a), &rx, ack, sizeof ack));
    CHECK_EQ(msg_state(&c, id), MSG_ACKED);
    CHECK_EQ(msg_next_tx(&c, 200000, info, sizeof info), 0);
    /* ack from the wrong station is ignored */
    id = msg_send(&c, "SP6ABC", "third", 300000);
    msg_next_tx(&c, 300000, info, sizeof info);
    a = ":SQ6EMM-7 :ack3";
    msg_on_rx(&c, "SQ6EMM-7", "SP9XYZ", a, strlen(a), &rx, ack, sizeof ack);
    CHECK_EQ(msg_state(&c, id), MSG_SENT_WAIT_ACK);

    /* incoming message for us with id: deliver + ack; duplicate: ack again, flagged */
    const char *m = ":SQ6EMM-7 :Hello{45";
    CHECK(msg_on_rx(&c, "SQ6EMM-7", "SP6ABC", m, strlen(m), &rx, ack, sizeof ack));
    CHECK(rx.valid); CHECK(!rx.duplicate); CHECK_STR(rx.text, "Hello"); CHECK_STR(rx.from, "SP6ABC");
    CHECK_STR(ack, ":SP6ABC   :ack45");
    CHECK(msg_on_rx(&c, "SQ6EMM-7", "SP6ABC", m, strlen(m), &rx, ack, sizeof ack));
    CHECK(rx.valid); CHECK(rx.duplicate);
    /* not for us */
    m = ":SP9XYZ   :Hello{46";
    CHECK(!msg_on_rx(&c, "SQ6EMM-7", "SP6ABC", m, strlen(m), &rx, ack, sizeof ack));
    CHECK(!rx.valid);
    /* message without id: deliver, no ack */
    m = ":SQ6EMM-7 :no ack";
    CHECK(!msg_on_rx(&c, "SQ6EMM-7", "SP6ABC", m, strlen(m), &rx, ack, sizeof ack));
    CHECK(rx.valid); CHECK_STR(rx.text, "no ack");

    /* outbox full */
    msg_ctx_t f; msg_init(&f);
    for (int i = 0; i < MSG_OUTBOX; i++) CHECK(msg_send(&f, "X", "y", 0) != 0);
    CHECK_EQ(msg_send(&f, "X", "y", 0), 0);
    TEST_END();
}
