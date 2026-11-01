#include "lora_sx1262.h"
#include "lora_aprs.h"
#include "test.h"

/* ---- mock SPI: records transactions, answers reads from a script ---- */
#define MAXT 64
static struct { uint8_t op; uint8_t args[300]; size_t n; } tr[MAXT];
static int ntr;
static bool dio1_level;
static uint16_t irq_to_report;
static uint8_t rx_payload[16] = "hello lora";
static uint8_t rx_len = 10;

static void mock_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
    (void)ctx;
    memset(rx, 0xA2, n);                       /* status byte: standby RC */
    if (ntr < MAXT) { tr[ntr].op = tx[0]; tr[ntr].n = n - 1; memcpy(tr[ntr].args, tx + 1, n - 1); ntr++; }
    switch (tx[0]) {
    case SX_OP_GET_IRQ_STATUS: rx[1] = (uint8_t)(irq_to_report >> 8); rx[2] = (uint8_t)irq_to_report; break;
    case SX_OP_GET_RX_BUFFER_STATUS: rx[1] = rx_len; rx[2] = 0x00; break;
    case SX_OP_READ_BUFFER: memcpy(rx + 3, rx_payload, rx_len); break;
    case SX_OP_GET_PACKET_STATUS: rx[1] = 180; rx[2] = (uint8_t)(int8_t)(-12 * 4); rx[3] = 180; break; /* -90 dBm, -3 dB */
    case SX_OP_READ_REGISTER: rx[4] = 0x00; break;
    default: break;
    }
}
static void mock_busy(void *c) { (void)c; }
static int resets; static void mock_reset(void *c) { (void)c; resets++; }
static bool mock_dio1(void *c) { (void)c; return dio1_level; }
static bool sw_tx, sw_rx; static void mock_sw(void *c, bool t, bool r) { (void)c; sw_tx = t; sw_rx = r; }

static int find_op(uint8_t op, int from)
{
    for (int i = from; i < ntr; i++) if (tr[i].op == op) return i;
    return -1;
}

int main(void)
{
    /* frequency register: 868 MHz -> 0x36400000 exactly; 433.775 MHz * 1.048576 = 454846054 = 0x1B1C6666 */
    CHECK_EQ(sx1262_freq_to_reg(868000000u), 0x36400000u);
    CHECK_EQ(sx1262_freq_to_reg(433775000u), 0x1B1C6666u);
    CHECK_EQ(sx1262_freq_to_reg(869525000u), (uint32_t)(869525000.0 * 33554432.0 / 32000000.0));

    lora_params_t p;
    lora_preset_aprs_433(&p);
    CHECK(sx1262_ldro_needed(&p));                     /* SF12 BW125: 32.8 ms symbols */
    lora_preset_meshtastic_eu868_longfast(&p);
    CHECK(!sx1262_ldro_needed(&p));                    /* SF11 BW250: 8.2 ms */
    lora_preset_pack_433(&p);
    CHECK(!sx1262_ldro_needed(&p));

    /* time on air sanity: SF12 BW125 CR4/5 8-byte preamble, 50 byte payload ~ 2.5 s */
    lora_preset_aprs_433(&p);
    uint32_t toa = sx1262_time_on_air_ms(&p, 50);
    printf("ToA SF12/125k 50 B: %u ms\n", (unsigned)toa);
    CHECK(toa > 2200 && toa < 2400);               /* (12.25 + 58 symbols) * 32.768 ms = 2302 ms */
    lora_preset_pack_433(&p);
    toa = sx1262_time_on_air_ms(&p, 50);
    printf("ToA SF9/125k 50 B: %u ms\n", (unsigned)toa);
    CHECK(toa > 300 && toa < 360);                 /* (12.25 + 68 symbols) * 4.096 ms = 329 ms */

    sx1262_hal_t hal = { .ctx = NULL, .spi_xfer = mock_xfer, .wait_busy = mock_busy, .reset = mock_reset,
                         .dio1 = mock_dio1, .rf_switch = mock_sw, .delay_ms = NULL };
    sx1262_cfg_t cfg = { .use_tcxo = true, .tcxo_voltage = 0x02, .dio2_rf_switch = false, .dcdc = true };
    sx1262_t d;
    ntr = 0;
    CHECK(sx1262_init(&d, &hal, &cfg));
    CHECK_EQ(resets, 1);
    CHECK_EQ(tr[0].op, SX_OP_SET_STANDBY);
    CHECK(find_op(SX_OP_SET_DIO3_TCXO, 0) >= 0);
    int i = find_op(SX_OP_SET_PACKET_TYPE, 0); CHECK(i >= 0); CHECK_EQ(tr[i].args[0], SX_PACKET_TYPE_LORA);
    i = find_op(SX_OP_SET_DIO_IRQ_PARAMS, 0); CHECK(i >= 0); CHECK_EQ(tr[i].args[1], 0x63); CHECK_EQ(tr[i].args[0], 0x02);
    i = find_op(SX_OP_WRITE_REGISTER, 0); CHECK(i >= 0);
    CHECK_EQ(d.state, SX_STATE_STANDBY);

    /* configure LoRa-APRS */
    ntr = 0;
    lora_preset_aprs_433(&p);
    CHECK(sx1262_configure(&d, &p));
    i = find_op(SX_OP_SET_RF_FREQUENCY, 0); CHECK(i >= 0);
    CHECK_EQ(tr[i].args[0], 0x1B); CHECK_EQ(tr[i].args[1], 0x1C); CHECK_EQ(tr[i].args[2], 0x66); CHECK_EQ(tr[i].args[3], 0x66);
    i = find_op(SX_OP_SET_MODULATION, 0); CHECK(i >= 0);
    CHECK_EQ(tr[i].args[0], 12); CHECK_EQ(tr[i].args[1], SX_BW_125K); CHECK_EQ(tr[i].args[2], 1); CHECK_EQ(tr[i].args[3], 1);
    i = find_op(SX_OP_SET_TX_PARAMS, 0); CHECK(i >= 0); CHECK_EQ(tr[i].args[0], 22);
    i = find_op(SX_OP_SET_PA_CONFIG, 0); CHECK(i >= 0); CHECK_EQ(tr[i].args[0], 0x04); CHECK_EQ(tr[i].args[1], 0x07);
    i = find_op(SX_OP_CALIBRATE_IMAGE, 0); CHECK(i >= 0); CHECK_EQ(tr[i].args[0], 0x6B);
    /* sync word 0x12 -> registers 0x14 0x24 */
    int w1 = find_op(SX_OP_WRITE_REGISTER, 0); int w2 = find_op(SX_OP_WRITE_REGISTER, w1 + 1);
    CHECK(w1 >= 0 && w2 >= 0);
    CHECK_EQ(tr[w1].args[0], 0x07); CHECK_EQ(tr[w1].args[1], 0x40); CHECK_EQ(tr[w1].args[2], 0x14);
    CHECK_EQ(tr[w2].args[1], 0x41); CHECK_EQ(tr[w2].args[2], 0x24);
    p.sf = 4; CHECK(!sx1262_configure(&d, &p)); p.sf = 12;
    p.power_dbm = 23; CHECK(!sx1262_configure(&d, &p)); p.power_dbm = 22;
    p.freq_hz = 100000000; CHECK(!sx1262_configure(&d, &p)); p.freq_hz = 433775000u;
    CHECK(sx1262_configure(&d, &p));

    /* send */
    ntr = 0;
    const uint8_t msg[] = "<\xff\x01SQ6EMM-7>APLRT1:!5106.50N/01702.25E[";
    CHECK(sx1262_send(&d, msg, (uint8_t)(sizeof msg - 1), 5000));
    i = find_op(SX_OP_WRITE_BUFFER, 0); CHECK(i >= 0);
    CHECK_EQ(tr[i].args[0], 0x00); CHECK(memcmp(tr[i].args + 1, msg, sizeof msg - 1) == 0);
    i = find_op(SX_OP_SET_PACKET_PARAMS, 0); CHECK(i >= 0); CHECK_EQ(tr[i].args[3], sizeof msg - 1);
    i = find_op(SX_OP_SET_TX, 0); CHECK(i >= 0);
    CHECK_EQ(tr[i].args[0], 0x04); CHECK_EQ(tr[i].args[1], 0xE2); CHECK_EQ(tr[i].args[2], 0x00); /* 5000 ms * 64 = 0x04E200 */
    CHECK(sw_tx && !sw_rx);
    CHECK_EQ(d.state, SX_STATE_TX);
    CHECK(!sx1262_send(&d, msg, 5, 100));            /* busy */
    sx_event_t ev;
    dio1_level = false;
    CHECK(!sx1262_service(&d, &ev));
    dio1_level = true; irq_to_report = SX_IRQ_TX_DONE;
    CHECK(sx1262_service(&d, &ev));
    CHECK_EQ(ev.kind, SX_EV_TX_DONE);
    CHECK_EQ(d.state, SX_STATE_STANDBY);
    CHECK(!sw_tx && !sw_rx);
    CHECK_EQ(d.tx_count, 1);

    /* receive continuous + packet */
    ntr = 0;
    CHECK(sx1262_receive(&d, 0));
    i = find_op(SX_OP_SET_RX, 0); CHECK(i >= 0);
    CHECK_EQ(tr[i].args[0], 0xFF); CHECK_EQ(tr[i].args[1], 0xFF); CHECK_EQ(tr[i].args[2], 0xFF);
    CHECK(sw_rx && !sw_tx);
    irq_to_report = SX_IRQ_RX_DONE;
    CHECK(sx1262_service(&d, &ev));
    CHECK_EQ(ev.kind, SX_EV_RX_DONE);
    CHECK_EQ(ev.len, 10);
    CHECK(memcmp(ev.payload, "hello lora", 10) == 0);
    CHECK_EQ(ev.rssi_dbm, -90);
    CHECK_EQ(ev.snr_db, -12);
    irq_to_report = SX_IRQ_RX_DONE | SX_IRQ_CRC_ERR;
    CHECK(sx1262_service(&d, &ev));
    CHECK_EQ(ev.kind, SX_EV_RX_CRC_ERROR);
    CHECK_EQ(d.crc_errors, 1);
    irq_to_report = SX_IRQ_TIMEOUT;
    CHECK(sx1262_service(&d, &ev));
    CHECK_EQ(ev.kind, SX_EV_TIMEOUT);
    CHECK(sx1262_sleep(&d));
    CHECK_EQ(d.state, SX_STATE_SLEEP);

    /* LoRa-APRS framing */
    uint8_t f[256];
    const char *info = "!5106.50N/01702.25E[mobile-radio";
    size_t n = lora_aprs_encode(f, sizeof f, "SQ6EMM-7", "APLRT1", "WIDE1-1", info, strlen(info));
    CHECK_EQ(n, 3 + 8 + 1 + 6 + 1 + 7 + 1 + strlen(info));
    CHECK_EQ(f[0], '<'); CHECK_EQ(f[1], 0xFF); CHECK_EQ(f[2], 0x01);
    CHECK(memcmp(f + 3, "SQ6EMM-7>APLRT1,WIDE1-1:!5106", 29) == 0);
    lora_aprs_pkt_t pk;
    CHECK(lora_aprs_decode(f, n, &pk));
    CHECK_STR(pk.src, "SQ6EMM-7"); CHECK_STR(pk.dst, "APLRT1"); CHECK_STR(pk.path, "WIDE1-1");
    CHECK_EQ(pk.info_len, strlen(info)); CHECK(memcmp(pk.info, info, pk.info_len) == 0);
    n = lora_aprs_encode(f, sizeof f, "SKI-7", "APLRT1", NULL, ":SKI-3    :hi{1", 15);
    CHECK(n > 0);
    CHECK(lora_aprs_decode(f, n, &pk)); CHECK_STR(pk.path, ""); CHECK_STR(pk.dst, "APLRT1");
    CHECK(!lora_aprs_decode((const uint8_t *)"garbage", 7, &pk));
    CHECK(!lora_aprs_decode(f, 5, &pk));
    CHECK_EQ(lora_aprs_encode(f, 10, "SQ6EMM-7", "APLRT1", NULL, info, strlen(info)), 0);
    CHECK_EQ(lora_aprs_encode(f, sizeof f, "WAYTOOLONGCALL", "APLRT1", NULL, info, strlen(info)), 0);
    TEST_END();
}
