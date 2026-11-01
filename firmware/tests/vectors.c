/* Prints reference byte vectors; tools/test_mrp.py compares them with the Python implementation. */
#include "afsk.h"
#include "ax25.h"
#include "mrp.h"
#include <stdio.h>
#include <string.h>

static void hex(const char *name, const uint8_t *b, size_t n)
{
    printf("%s ", name);
    for (size_t i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

int main(void)
{
    uint8_t f[MRP_MAX_FRAME], pl[128];
    mrp_ptt_t p = { 1, 0 };
    hex("PTT_KEY_SEQ1", f, mrp_encode(f, sizeof f, MRP_PTT, 1, pl, (uint16_t)mrp_pack_ptt(&p, pl)));
    mrp_set_freq_t fq = { 433500000u, 4, 1, 0, 12, 0 };
    hex("SET_FREQ_SEQ2", f, mrp_encode(f, sizeof f, MRP_SET_FREQ, 2, pl, (uint16_t)mrp_pack_set_freq(&fq, pl)));
    mrp_status_t st = { .flags = 0x11, .rssi_dbm = -101, .channel = 16, .batt_mv = 7612, .batt_ma = -850, .soc_pct = 77,
                        .temp_c = -5, .vbus_mv = 9000, .vbus_ma = 1500, .pd_role = 1, .pd_mv = 9000, .pd_ma = 2000,
                        .tx_remaining_s = 179, .mode = 1, .profile = 0, .dv_flags = 3, .pwr_flags = 0x0B, .freq_hz = 433500000u,
                        .in_mv = 20000, .in_ma = 2250 };
    hex("STATUS_SEQ3", f, mrp_encode(f, sizeof f, MRP_STATUS, 3, pl, (uint16_t)mrp_pack_status(&st, pl)));
    size_t n = mrp_pack_send_msg(pl, sizeof pl, MRP_TR_FM, "SP6ABC-9", "see you at 18");
    hex("SEND_MSG_SEQ4", f, mrp_encode(f, sizeof f, MRP_SEND_MSG, 4, pl, (uint16_t)n));

    ax25_addr_t dst, src, digi;
    ax25_addr_from_str(&dst, "APZMRP"); ax25_addr_from_str(&src, "SQ6EMM-7"); ax25_addr_from_str(&digi, "WIDE1-1");
    const char *info = "!5106.50N/01702.25E[mobile-radio";
    uint8_t fr[AX25_MAX_FRAME];
    size_t fl = ax25_build_ui(fr, sizeof fr, &dst, &src, &digi, 1, (const uint8_t *)info, strlen(info));
    hex("AX25_UI", fr, fl);
    static uint8_t bits[8000];
    size_t nb = hdlc_encode(fr, fl, 2, 1, bits, sizeof bits, NULL);
    printf("HDLC_LEVELS ");
    for (size_t i = 0; i < nb; i++) putchar(bits[i] ? '1' : '0');
    printf("\n");
    afsk_mod_t m; afsk_mod_init(&m, 48000, 8000);
    static int16_t pcm[400000];
    size_t ns = afsk_mod_run(&m, bits, nb, pcm, sizeof pcm / sizeof pcm[0]);
    printf("AFSK48K_SAMPLES %zu\n", ns);
    printf("AFSK48K_FIRST16");
    for (int i = 0; i < 16; i++) printf(" %d", pcm[i]);
    printf("\n");
    return 0;
}
