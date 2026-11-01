#include "dv.h"
#include "test.h"

int main(void)
{
    dv_t dv;
    CHECK(dv_open(&dv));
    CHECK_EQ(dv.n_speech, 320);
    CHECK_EQ(dv.n_nom_modem, 1920);

    /* TX: 640 samples of 16 kHz speech (40 ms) -> exactly one 1920-sample modem frame */
    static int16_t speech16[640], modem[DV_MODEM_MAX], back16[1024];
    for (int i = 0; i < 640; i++) speech16[i] = (int16_t)(1000 + i);
    CHECK_EQ(dv_tx_push(&dv, speech16, 320, modem, DV_MODEM_MAX), 0);        /* half a frame: nothing yet */
    CHECK_EQ(dv_tx_push(&dv, speech16 + 320, 320, modem, DV_MODEM_MAX), 1920);
    CHECK_EQ(dv.frames_tx, 1);
    CHECK_EQ(dv_tx_push(&dv, speech16, 0, modem, DV_MODEM_MAX), 0);
    /* pushing two frames at once yields one, the second is buffered until the next call */
    CHECK_EQ(dv_tx_push(&dv, speech16, 640, modem, DV_MODEM_MAX), 1920);
    CHECK_EQ(dv.speech_fill, 0);

    /* RX: feed the modem frame in odd-sized chunks -> one 640-sample 16 kHz speech block */
    size_t out = 0, off = 0;
    while (off < 1920) {
        size_t len = (1920 - off < 97) ? 1920 - off : 97;
        size_t r = dv_rx_push(&dv, modem + off, len, back16, 1024);
        if (r) out = r;
        off += len;
    }
    CHECK_EQ(out, 640);
    CHECK(dv_in_sync(&dv));
    /* with the transparent stub, decimation+interpolation of a ramp reproduces it within 1 LSB on odd samples */
    int bad = 0;
    for (int i = 3; i < 640; i += 2) if (abs(back16[i] - speech16[i]) > 1) bad++;
    CHECK_EQ(bad, 0);
    dv_close(&dv);
    CHECK(dv.fdv == NULL);
    TEST_END();
}
