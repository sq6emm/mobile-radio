#include "lora_sx1262.h"
#include <string.h>

static void cmd(sx1262_t *d, uint8_t op, const uint8_t *args, size_t n, uint8_t *rx, size_t rxn)
{
    uint8_t tx[1 + 16 + SX_MAX_PAYLOAD], rxb[1 + 16 + SX_MAX_PAYLOAD];
    size_t total = 1 + n + rxn;
    if (total > sizeof tx) return;
    tx[0] = op;
    if (n) memcpy(tx + 1, args, n);
    if (rxn) memset(tx + 1 + n, 0, rxn);
    d->hal.wait_busy(d->hal.ctx);
    d->hal.spi_xfer(d->hal.ctx, tx, rxb, total);
    d->last_status = rxb[0];
    if (rx && rxn) memcpy(rx, rxb + 1 + n, rxn);
}

static void write_reg(sx1262_t *d, uint16_t addr, uint8_t val)
{
    uint8_t a[3] = { (uint8_t)(addr >> 8), (uint8_t)addr, val };
    cmd(d, SX_OP_WRITE_REGISTER, a, 3, NULL, 0);
}

static uint8_t read_reg(sx1262_t *d, uint16_t addr)
{
    uint8_t a[3] = { (uint8_t)(addr >> 8), (uint8_t)addr, 0 };  /* 1 NOP byte then data */
    uint8_t r[1];
    cmd(d, SX_OP_READ_REGISTER, a, 3, r, 1);
    return r[0];
}

uint32_t sx1262_freq_to_reg(uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz << 25) / 32000000ull);
}

bool sx1262_ldro_needed(const lora_params_t *p)
{
    /* symbol time = 2^SF / BW; LDRO when > 16 ms */
    uint32_t bw_hz = p->bw == SX_BW_62K5 ? 62500 : p->bw == SX_BW_125K ? 125000 : p->bw == SX_BW_250K ? 250000 : 500000;
    uint32_t sym_us = (uint32_t)(((uint64_t)1000000u << p->sf) / bw_hz);
    return sym_us > 16000u;
}

bool sx1262_init(sx1262_t *d, const sx1262_hal_t *hal, const sx1262_cfg_t *cfg)
{
    memset(d, 0, sizeof(*d));
    d->hal = *hal;
    d->cfg = *cfg;
    if (!d->hal.spi_xfer || !d->hal.wait_busy || !d->hal.reset || !d->hal.dio1) return false;

    d->hal.reset(d->hal.ctx);
    uint8_t stdby_rc = 0x00;
    cmd(d, SX_OP_SET_STANDBY, &stdby_rc, 1, NULL, 0);
    if (cfg->use_tcxo) {
        /* DIO3 as TCXO control: voltage, 5 ms start-up (delay in 15.625 us units: 320 = 0x140) */
        uint8_t a[4] = { cfg->tcxo_voltage, 0x00, 0x01, 0x40 };
        cmd(d, SX_OP_SET_DIO3_TCXO, a, 4, NULL, 0);
        uint8_t cal = 0x7F;
        cmd(d, SX_OP_CALIBRATE, &cal, 1, NULL, 0);
        if (d->hal.delay_ms) d->hal.delay_ms(d->hal.ctx, 5);
    }
    uint8_t reg_mode = cfg->dcdc ? 0x01 : 0x00;
    cmd(d, SX_OP_SET_REGULATOR_MODE, &reg_mode, 1, NULL, 0);
    if (cfg->dio2_rf_switch) {
        uint8_t en = 0x01;
        cmd(d, SX_OP_SET_DIO2_RF_SWITCH, &en, 1, NULL, 0);
    }
    uint8_t pt = SX_PACKET_TYPE_LORA;
    cmd(d, SX_OP_SET_PACKET_TYPE, &pt, 1, NULL, 0);
    uint8_t base[2] = { 0x00, 0x00 };
    cmd(d, SX_OP_SET_BUFFER_BASE, base, 2, NULL, 0);
    /* IRQs on DIO1: TxDone, RxDone, HeaderErr, CrcErr, Timeout */
    uint16_t mask = SX_IRQ_TX_DONE | SX_IRQ_RX_DONE | SX_IRQ_HEADER_ERR | SX_IRQ_CRC_ERR | SX_IRQ_TIMEOUT;
    uint8_t irq[8] = { (uint8_t)(mask >> 8), (uint8_t)mask, (uint8_t)(mask >> 8), (uint8_t)mask, 0, 0, 0, 0 };
    cmd(d, SX_OP_SET_DIO_IRQ_PARAMS, irq, 8, NULL, 0);
    /* SX1262 errata: TX clamp for +22 dBm, OCP 140 mA */
    write_reg(d, SX_REG_TX_CLAMP, (uint8_t)(read_reg(d, SX_REG_TX_CLAMP) | 0x1E));
    write_reg(d, SX_REG_OCP, 0x38);
    d->state = SX_STATE_STANDBY;
    return true;
}

bool sx1262_configure(sx1262_t *d, const lora_params_t *p)
{
    if (p->sf < 5 || p->sf > 12 || p->cr < 1 || p->cr > 4 || p->power_dbm < -9 || p->power_dbm > 22) return false;
    if (p->freq_hz < 150000000u || p->freq_hz > 960000000u) return false;
    d->params = *p;
    sx1262_standby(d);

    /* image calibration band */
    uint8_t img[2];
    if (p->freq_hz < 460000000u) { img[0] = 0x6B; img[1] = 0x6F; }         /* 430-440 MHz */
    else if (p->freq_hz < 900000000u) { img[0] = 0xD7; img[1] = 0xDB; }    /* 863-870 MHz */
    else { img[0] = 0xE1; img[1] = 0xE9; }
    cmd(d, SX_OP_CALIBRATE_IMAGE, img, 2, NULL, 0);

    uint32_t fr = sx1262_freq_to_reg(p->freq_hz);
    uint8_t f[4] = { (uint8_t)(fr >> 24), (uint8_t)(fr >> 16), (uint8_t)(fr >> 8), (uint8_t)fr };
    cmd(d, SX_OP_SET_RF_FREQUENCY, f, 4, NULL, 0);

    /* PA: SX1262 +22 dBm optimal settings (paDutyCycle 0x04, hpMax 0x07, deviceSel 0x00, paLut 0x01) */
    uint8_t pa[4] = { 0x04, 0x07, 0x00, 0x01 };
    if (p->power_dbm <= 14) { pa[0] = 0x02; pa[1] = 0x02; }
    else if (p->power_dbm <= 17) { pa[0] = 0x02; pa[1] = 0x03; }
    else if (p->power_dbm <= 20) { pa[0] = 0x03; pa[1] = 0x05; }
    cmd(d, SX_OP_SET_PA_CONFIG, pa, 4, NULL, 0);
    uint8_t txp[2] = { (uint8_t)p->power_dbm, 0x04 };   /* ramp 200 us */
    cmd(d, SX_OP_SET_TX_PARAMS, txp, 2, NULL, 0);

    uint8_t mod[4] = { p->sf, (uint8_t)p->bw, p->cr, sx1262_ldro_needed(p) ? 1 : 0 };
    cmd(d, SX_OP_SET_MODULATION, mod, 4, NULL, 0);

    uint8_t pkt[6] = { (uint8_t)(p->preamble_len >> 8), (uint8_t)p->preamble_len,
                       p->implicit_header ? 1 : 0, SX_MAX_PAYLOAD, p->crc_on ? 1 : 0, p->iq_inverted ? 1 : 0 };
    cmd(d, SX_OP_SET_PACKET_PARAMS, pkt, 6, NULL, 0);

    write_reg(d, SX_REG_LORA_SYNC_MSB, (uint8_t)(p->sync_word >> 8));
    write_reg(d, SX_REG_LORA_SYNC_LSB, (uint8_t)p->sync_word);
    return true;
}

static void set_packet_len(sx1262_t *d, uint8_t len)
{
    const lora_params_t *p = &d->params;
    uint8_t pkt[6] = { (uint8_t)(p->preamble_len >> 8), (uint8_t)p->preamble_len,
                       p->implicit_header ? 1 : 0, len, p->crc_on ? 1 : 0, p->iq_inverted ? 1 : 0 };
    cmd(d, SX_OP_SET_PACKET_PARAMS, pkt, 6, NULL, 0);
}

bool sx1262_send(sx1262_t *d, const uint8_t *data, uint8_t len, uint32_t timeout_ms)
{
    if (!data || len == 0) return false;
    if (d->state == SX_STATE_TX) return false;
    sx1262_standby(d);
    uint8_t buf[1 + SX_MAX_PAYLOAD];
    buf[0] = 0x00;                          /* offset */
    memcpy(buf + 1, data, len);
    cmd(d, SX_OP_WRITE_BUFFER, buf, (size_t)len + 1, NULL, 0);
    set_packet_len(d, len);
    uint8_t clr[2] = { 0xFF, 0xFF };
    cmd(d, SX_OP_CLEAR_IRQ_STATUS, clr, 2, NULL, 0);
    if (d->hal.rf_switch) d->hal.rf_switch(d->hal.ctx, true, false);
    uint32_t t = (timeout_ms * 64u);        /* 15.625 us units */
    if (t > 0xFFFFFF) t = 0xFFFFFF;
    uint8_t to[3] = { (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t };
    cmd(d, SX_OP_SET_TX, to, 3, NULL, 0);
    d->state = SX_STATE_TX;
    return true;
}

bool sx1262_receive(sx1262_t *d, uint32_t timeout_ms)
{
    sx1262_standby(d);
    set_packet_len(d, SX_MAX_PAYLOAD);
    uint8_t clr[2] = { 0xFF, 0xFF };
    cmd(d, SX_OP_CLEAR_IRQ_STATUS, clr, 2, NULL, 0);
    if (d->hal.rf_switch) d->hal.rf_switch(d->hal.ctx, false, true);
    uint32_t t = timeout_ms ? timeout_ms * 64u : 0xFFFFFF;   /* 0xFFFFFF = continuous */
    if (t > 0xFFFFFF) t = 0xFFFFFF;
    uint8_t to[3] = { (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t };
    cmd(d, SX_OP_SET_RX, to, 3, NULL, 0);
    d->state = SX_STATE_RX;
    return true;
}

bool sx1262_standby(sx1262_t *d)
{
    uint8_t rc = 0x00;
    cmd(d, SX_OP_SET_STANDBY, &rc, 1, NULL, 0);
    if (d->hal.rf_switch) d->hal.rf_switch(d->hal.ctx, false, false);
    d->state = SX_STATE_STANDBY;
    return true;
}

bool sx1262_sleep(sx1262_t *d)
{
    uint8_t cfg = 0x04;                     /* warm start, retain config */
    if (d->hal.rf_switch) d->hal.rf_switch(d->hal.ctx, false, false);
    cmd(d, SX_OP_SET_SLEEP, &cfg, 1, NULL, 0);
    d->state = SX_STATE_SLEEP;
    return true;
}

bool sx1262_service(sx1262_t *d, sx_event_t *ev)
{
    ev->kind = SX_EV_NONE;
    if (!d->hal.dio1(d->hal.ctx)) return false;
    uint8_t irq[2];
    cmd(d, SX_OP_GET_IRQ_STATUS, NULL, 0, irq, 2);
    uint16_t flags = (uint16_t)((irq[0] << 8) | irq[1]);
    uint8_t clr[2] = { irq[0], irq[1] };
    cmd(d, SX_OP_CLEAR_IRQ_STATUS, clr, 2, NULL, 0);
    if (flags & SX_IRQ_TX_DONE) {
        ev->kind = SX_EV_TX_DONE;
        d->tx_count++;
        sx1262_standby(d);
        return true;
    }
    if (flags & SX_IRQ_RX_DONE) {
        if (flags & (SX_IRQ_CRC_ERR | SX_IRQ_HEADER_ERR)) {
            ev->kind = SX_EV_RX_CRC_ERROR;
            d->crc_errors++;
            return true;
        }
        uint8_t bs[2];
        cmd(d, SX_OP_GET_RX_BUFFER_STATUS, NULL, 0, bs, 2);  /* len, start offset */
        uint8_t len = bs[0], off = bs[1];
        uint8_t arg[2] = { off, 0x00 };                       /* offset + NOP */
        cmd(d, SX_OP_READ_BUFFER, arg, 2, ev->payload, len);
        ev->len = len;
        uint8_t ps[3];
        cmd(d, SX_OP_GET_PACKET_STATUS, NULL, 0, ps, 3);      /* RssiPkt, SnrPkt, SignalRssiPkt */
        ev->rssi_dbm = (int16_t)(-(int)ps[0] / 2);
        ev->snr_db = (int8_t)(((int8_t)ps[1]) / 4);
        ev->kind = SX_EV_RX_DONE;
        d->rx_count++;
        return true;
    }
    if (flags & SX_IRQ_TIMEOUT) {
        ev->kind = SX_EV_TIMEOUT;
        sx1262_standby(d);
        return true;
    }
    return false;
}

uint32_t sx1262_time_on_air_ms(const lora_params_t *p, uint8_t len)
{
    uint32_t bw_hz = p->bw == SX_BW_62K5 ? 62500 : p->bw == SX_BW_125K ? 125000 : p->bw == SX_BW_250K ? 250000 : 500000;
    double tsym_ms = (double)(1u << p->sf) * 1000.0 / bw_hz;
    int de = sx1262_ldro_needed(p) ? 1 : 0;
    int ih = p->implicit_header ? 1 : 0;
    int crc = p->crc_on ? 1 : 0;
    double num = 8.0 * len - 4.0 * p->sf + 28 + 16 * crc - 20 * ih;
    double den = 4.0 * (p->sf - 2 * de);
    double n = num / den;
    long ceiln = (long)n; if (ceiln < n) ceiln++;
    if (ceiln < 0) ceiln = 0;
    double payload_syms = 8 + ceiln * (p->cr + 4);
    double total = (p->preamble_len + 4.25 + payload_syms) * tsym_ms;
    return (uint32_t)(total + 0.999);
}

void lora_preset_aprs_433(lora_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->freq_hz = 433775000u; p->sf = 12; p->bw = SX_BW_125K; p->cr = 1;
    p->preamble_len = 8; p->power_dbm = 22; p->sync_word = 0x1424; p->crc_on = true;
}

void lora_preset_meshtastic_eu868_longfast(lora_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->freq_hz = 869525000u; p->sf = 11; p->bw = SX_BW_250K; p->cr = 1;
    p->preamble_len = 16; p->power_dbm = 22; p->sync_word = 0x2B44; p->crc_on = true;
}

void lora_preset_pack_433(lora_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->freq_hz = 433900000u; p->sf = 9; p->bw = SX_BW_125K; p->cr = 1;
    p->preamble_len = 8; p->power_dbm = 10; p->sync_word = 0x1424; p->crc_on = true;   /* ISM 10 mW default */
}

void lora_preset_pack_868(lora_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->freq_hz = 869525000u; p->sf = 9; p->bw = SX_BW_125K; p->cr = 1;
    p->preamble_len = 8; p->power_dbm = 22; p->sync_word = 0x1424; p->crc_on = true;   /* 869.4-869.65: 500 mW ERP, 10 % duty */
}
