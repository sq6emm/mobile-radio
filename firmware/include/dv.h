/*
 * Digital voice (FreeDV 2400B via the Codec2 library) glue.
 *
 * FreeDV 2400B: Codec2 1300 bit/s + FSK modem designed to pass through the
 * audio path of an ordinary FM radio. Speech side is 8 kHz, modem side is
 * 48 kHz, exactly the sample rate of our ADC/DAC path. Amateur mode only.
 *
 * The real implementation links firmware/third_party/codec2 (LGPL 2.1).
 * The host unit test links port/host/freedv_stub.c, which implements the
 * same freedv_* API as a transparent pass-through so the buffering logic
 * here can be verified without the library.
 */
#ifndef DV_H
#define DV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DV_SPEECH_RATE   8000
#define DV_MODEM_RATE    48000
#define DV_SPEECH_FRAME  320        /* 40 ms at 8 kHz (2400B) */
#define DV_MODEM_MAX     2400       /* >= freedv_get_n_max_modem_samples() */

struct freedv;

typedef struct {
    struct freedv *fdv;
    int n_speech, n_nom_modem, n_max_modem;
    /* TX: accumulate 16 kHz speech from the phone, decimate to 8 kHz, encode per frame */
    int16_t  speech_acc[DV_SPEECH_FRAME];
    int      speech_fill;
    int16_t  dec_prev;
    /* RX: feed 48 kHz modem samples in freedv_nin()-sized chunks */
    int16_t  modem_acc[DV_MODEM_MAX];
    int      modem_fill;
    bool     sync;
    uint32_t frames_tx, frames_rx;
} dv_t;

bool dv_open(dv_t *dv);
void dv_close(dv_t *dv);

/* Push 16 kHz speech (any count). When a full 40 ms frame is ready, writes
 * n_nom_modem samples of 48 kHz modem audio to 'modem_out' and returns that
 * count, else 0. Call repeatedly until it returns 0 after each push. */
size_t dv_tx_push(dv_t *dv, const int16_t *speech16k, size_t n, int16_t *modem_out, size_t cap);

/* Push 48 kHz modem audio from the receiver. When a frame decodes, writes
 * 16 kHz speech (2 * n_speech samples) to 'speech_out' and returns the count. */
size_t dv_rx_push(dv_t *dv, const int16_t *modem48k, size_t n, int16_t *speech_out, size_t cap);

bool dv_in_sync(const dv_t *dv);

#endif
