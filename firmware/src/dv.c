#include "dv.h"
#include "freedv_api.h"
#include <string.h>

bool dv_open(dv_t *dv)
{
    memset(dv, 0, sizeof(*dv));
    dv->fdv = freedv_open(FREEDV_MODE_2400B);
    if (!dv->fdv) return false;
    dv->n_speech = freedv_get_n_speech_samples(dv->fdv);
    dv->n_nom_modem = freedv_get_n_nom_modem_samples(dv->fdv);
    dv->n_max_modem = freedv_get_n_max_modem_samples(dv->fdv);
    if (dv->n_speech > DV_SPEECH_FRAME || dv->n_max_modem > DV_MODEM_MAX ||
        freedv_get_speech_sample_rate(dv->fdv) != DV_SPEECH_RATE ||
        freedv_get_modem_sample_rate(dv->fdv) != DV_MODEM_RATE) {
        freedv_close(dv->fdv);
        dv->fdv = NULL;
        return false;
    }
    return true;
}

void dv_close(dv_t *dv)
{
    if (dv->fdv) freedv_close(dv->fdv);
    dv->fdv = NULL;
}

size_t dv_tx_push(dv_t *dv, const int16_t *speech16k, size_t n, int16_t *modem_out, size_t cap)
{
    if (!dv->fdv || (int)cap < dv->n_nom_modem) return 0;
    size_t produced = 0;
    for (size_t i = 0; i < n; i++) {
        /* 16 -> 8 kHz: average of pairs (a 2-tap box filter; adequate for 300-3400 Hz speech) */
        if (i & 1) {
            dv->speech_acc[dv->speech_fill++] = (int16_t)(((int32_t)dv->dec_prev + speech16k[i]) / 2);
        } else {
            dv->dec_prev = speech16k[i];
        }
        if (dv->speech_fill == dv->n_speech) {
            if (produced) return produced;           /* caller must drain one frame at a time */
            freedv_tx(dv->fdv, modem_out, dv->speech_acc);
            dv->speech_fill = 0;
            dv->frames_tx++;
            produced = (size_t)dv->n_nom_modem;
        }
    }
    return produced;
}

size_t dv_rx_push(dv_t *dv, const int16_t *modem48k, size_t n, int16_t *speech_out, size_t cap)
{
    if (!dv->fdv || (int)cap < 2 * dv->n_speech) return 0;
    size_t produced = 0;
    for (size_t i = 0; i < n; i++) {
        dv->modem_acc[dv->modem_fill++] = modem48k[i];
        int nin = freedv_nin(dv->fdv);
        if (dv->modem_fill >= nin) {
            int16_t speech8[DV_SPEECH_FRAME];
            int nout = freedv_rx(dv->fdv, speech8, dv->modem_acc);
            dv->modem_fill = 0;
            dv->sync = freedv_get_sync(dv->fdv) != 0;
            if (nout > 0 && produced == 0) {
                /* 8 -> 16 kHz: linear interpolation */
                int16_t prev = 0;
                for (int k = 0; k < nout; k++) {
                    speech_out[2 * k] = (int16_t)(((int32_t)prev + speech8[k]) / 2);
                    speech_out[2 * k + 1] = speech8[k];
                    prev = speech8[k];
                }
                produced = (size_t)nout * 2;
                dv->frames_rx++;
            }
        }
    }
    return produced;
}

bool dv_in_sync(const dv_t *dv)
{
    return dv->sync;
}
