/*
 * Semtech SX1262 LoRa transceiver driver (command level), hardware-agnostic.
 *
 * Two instances are used: 433 MHz (E22-400M22S class, shares the helix) and
 * 868 MHz (E22-900M22S class, own antenna). All bus access goes through the
 * sx1262_hal_t callbacks so the driver can be unit tested with a mock SPI.
 *
 * Only the LoRa packet mode is implemented (no FSK). Opcodes and register
 * addresses follow the SX1261/2 datasheet (DS.SX1261-2.W.APP).
 */
#ifndef LORA_SX1262_H
#define LORA_SX1262_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* opcodes */
#define SX_OP_SET_SLEEP            0x84
#define SX_OP_SET_STANDBY          0x80
#define SX_OP_SET_TX               0x83
#define SX_OP_SET_RX               0x82
#define SX_OP_SET_CAD              0xC5
#define SX_OP_SET_REGULATOR_MODE   0x96
#define SX_OP_CALIBRATE            0x89
#define SX_OP_CALIBRATE_IMAGE      0x98
#define SX_OP_SET_PA_CONFIG        0x95
#define SX_OP_SET_RX_TX_FALLBACK   0x93
#define SX_OP_WRITE_REGISTER       0x0D
#define SX_OP_READ_REGISTER        0x1D
#define SX_OP_WRITE_BUFFER         0x0E
#define SX_OP_READ_BUFFER          0x1E
#define SX_OP_SET_DIO_IRQ_PARAMS   0x08
#define SX_OP_GET_IRQ_STATUS       0x12
#define SX_OP_CLEAR_IRQ_STATUS     0x02
#define SX_OP_SET_DIO2_RF_SWITCH   0x9D
#define SX_OP_SET_DIO3_TCXO        0x97
#define SX_OP_SET_RF_FREQUENCY     0x86
#define SX_OP_SET_PACKET_TYPE      0x8A
#define SX_OP_SET_TX_PARAMS        0x8E
#define SX_OP_SET_MODULATION       0x8B
#define SX_OP_SET_PACKET_PARAMS    0x8C
#define SX_OP_SET_BUFFER_BASE      0x8F
#define SX_OP_SET_SYMB_TIMEOUT     0xA0
#define SX_OP_GET_STATUS           0xC0
#define SX_OP_GET_RX_BUFFER_STATUS 0x13
#define SX_OP_GET_PACKET_STATUS    0x14
#define SX_OP_GET_DEVICE_ERRORS    0x17
#define SX_OP_CLEAR_DEVICE_ERRORS  0x07

#define SX_REG_LORA_SYNC_MSB       0x0740
#define SX_REG_LORA_SYNC_LSB       0x0741
#define SX_REG_OCP                 0x08E7
#define SX_REG_TX_CLAMP            0x08D8

#define SX_IRQ_TX_DONE     0x0001
#define SX_IRQ_RX_DONE     0x0002
#define SX_IRQ_HEADER_ERR  0x0020
#define SX_IRQ_CRC_ERR     0x0040
#define SX_IRQ_TIMEOUT     0x0200

#define SX_PACKET_TYPE_LORA 0x01
#define SX_MAX_PAYLOAD      255

typedef enum { SX_BW_62K5 = 0x03, SX_BW_125K = 0x04, SX_BW_250K = 0x05, SX_BW_500K = 0x06 } sx_bw_t;

typedef struct {
    uint32_t freq_hz;
    uint8_t  sf;            /* 5..12 */
    sx_bw_t  bw;
    uint8_t  cr;            /* 1..4 -> 4/5..4/8 */
    uint16_t preamble_len;
    int8_t   power_dbm;     /* -9..22 */
    uint16_t sync_word;     /* 0x1424 private (0x12), 0x3444 public (0x34), Meshtastic 0x2B44 */
    bool     crc_on;
    bool     iq_inverted;
    bool     implicit_header;
} lora_params_t;

typedef struct {
    void *ctx;
    /* full-duplex SPI transaction with NSS asserted for the whole transfer */
    void (*spi_xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n);
    void (*wait_busy)(void *ctx);
    void (*reset)(void *ctx);
    bool (*dio1)(void *ctx);
    void (*rf_switch)(void *ctx, bool tx, bool rx);  /* module TXEN/RXEN; may be NULL */
    void (*delay_ms)(void *ctx, uint32_t ms);
} sx1262_hal_t;

typedef struct {
    bool use_tcxo;          /* E22 modules: TCXO on DIO3 */
    uint8_t tcxo_voltage;   /* 0x00 = 1.6 V ... 0x02 = 1.8 V ... 0x07 = 3.3 V */
    bool dio2_rf_switch;    /* DIO2 drives the module's TX switch */
    bool dcdc;              /* use the DC-DC regulator */
} sx1262_cfg_t;

typedef enum { SX_EV_NONE = 0, SX_EV_TX_DONE, SX_EV_RX_DONE, SX_EV_RX_CRC_ERROR, SX_EV_TIMEOUT } sx_event_kind_t;

typedef struct {
    sx_event_kind_t kind;
    uint8_t  payload[SX_MAX_PAYLOAD];
    uint8_t  len;
    int16_t  rssi_dbm;
    int8_t   snr_db;
} sx_event_t;

typedef enum { SX_STATE_OFF = 0, SX_STATE_STANDBY, SX_STATE_TX, SX_STATE_RX, SX_STATE_SLEEP } sx_state_t;

typedef struct {
    sx1262_hal_t hal;
    sx1262_cfg_t cfg;
    lora_params_t params;
    sx_state_t state;
    uint8_t  last_status;
    uint32_t tx_count, rx_count, crc_errors;
} sx1262_t;

bool sx1262_init(sx1262_t *d, const sx1262_hal_t *hal, const sx1262_cfg_t *cfg);
bool sx1262_configure(sx1262_t *d, const lora_params_t *p);
bool sx1262_send(sx1262_t *d, const uint8_t *data, uint8_t len, uint32_t timeout_ms);
bool sx1262_receive(sx1262_t *d, uint32_t timeout_ms);          /* 0 = continuous */
bool sx1262_standby(sx1262_t *d);
bool sx1262_sleep(sx1262_t *d);
/* Poll DIO1; on an event fills *ev and returns true. */
bool sx1262_service(sx1262_t *d, sx_event_t *ev);
/* Time on air in ms for the current params (Semtech formula). */
uint32_t sx1262_time_on_air_ms(const lora_params_t *p, uint8_t payload_len);
/* Frequency to the 32 MHz / 2^25 register value. */
uint32_t sx1262_freq_to_reg(uint32_t hz);
bool sx1262_ldro_needed(const lora_params_t *p);

/* Presets */
void lora_preset_aprs_433(lora_params_t *p);        /* 433.775 MHz SF12 BW125 CR4/5, sync 0x12 */
void lora_preset_meshtastic_eu868_longfast(lora_params_t *p);  /* PHY only: 869.525 MHz BW250 SF11 CR4/5 sync 0x2B */
void lora_preset_pack_433(lora_params_t *p);        /* pack-to-pack 433.775... no: 433.900 SF9 BW125 */
void lora_preset_pack_868(lora_params_t *p);        /* pack-to-pack 869.4-869.65 band, SF9 BW125 */

#endif
