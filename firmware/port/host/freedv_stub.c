/*
 * Transparent stand-in for the Codec2 FreeDV API, used ONLY by the host unit
 * test of dv.c. It is not a vocoder or modem: speech frames are carried
 * inside the "modem" sample stream unchanged (8 kHz speech repeated 6x to
 * fill 48 kHz), so the glue's buffering, frame sizes and nin() handling can
 * be checked. Build the real thing by linking third_party/codec2.
 */
#include "freedv_api.h"
#include <stdlib.h>
#include <string.h>

#define N_SPEECH 320
#define N_MODEM  1920

struct freedv {
    int mode;
    int sync;
};

struct freedv *freedv_open(int mode)
{
    if (mode != FREEDV_MODE_2400B) return NULL;
    struct freedv *f = calloc(1, sizeof *f);
    f->mode = mode;
    return f;
}
void freedv_close(struct freedv *f) { free(f); }
void freedv_tx(struct freedv *f, short mod_out[], short speech_in[])
{
    (void)f;
    for (int i = 0; i < N_SPEECH; i++)
        for (int k = 0; k < 6; k++) mod_out[i * 6 + k] = speech_in[i];
}
int freedv_nin(struct freedv *f) { (void)f; return N_MODEM; }
int freedv_rx(struct freedv *f, short speech_out[], short demod_in[])
{
    f->sync = 1;
    for (int i = 0; i < N_SPEECH; i++) speech_out[i] = demod_in[i * 6];
    return N_SPEECH;
}
int freedv_get_speech_sample_rate(struct freedv *f) { (void)f; return 8000; }
int freedv_get_n_speech_samples(struct freedv *f) { (void)f; return N_SPEECH; }
int freedv_get_modem_sample_rate(struct freedv *f) { (void)f; return 48000; }
int freedv_get_n_max_modem_samples(struct freedv *f) { (void)f; return N_MODEM + 48; }
int freedv_get_n_nom_modem_samples(struct freedv *f) { (void)f; return N_MODEM; }
int freedv_get_sync(struct freedv *f) { return f->sync; }
