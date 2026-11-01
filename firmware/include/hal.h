/*
 * Port layer: everything the application needs from the hardware.
 * Implemented once per target (port/stm32f405/, port/host/ for simulation).
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- time ------------------------------------------------------------ */
uint32_t hal_millis(void);
void     hal_delay_ms(uint32_t ms);
void     hal_watchdog_kick(void);

/* --- USB vendor bulk interface (MRP transport) ----------------------- */
bool     hal_usb_connected(void);
size_t   hal_usb_read(uint8_t *buf, size_t cap);            /* non-blocking */
size_t   hal_usb_write(const uint8_t *buf, size_t len);     /* non-blocking, returns bytes queued */
void     hal_usb_reenumerate_with_uac(bool enable);         /* soft-disconnect and re-attach with/without UAC */

/* --- radio module (SA818) ------------------------------------------- */
void     hal_radio_power(bool on);                          /* PD pin */
void     hal_radio_ptt(bool key);                           /* PTT pin, active low on the module */
void     hal_radio_high_power(bool high);                   /* H/L pin */
bool     hal_radio_squelch_open(void);                      /* SQ pin, low = carrier */
size_t   hal_radio_uart_write(const char *s, size_t len);
size_t   hal_radio_uart_read_line(char *buf, size_t cap, uint32_t timeout_ms);

/* --- audio ----------------------------------------------------------- */
/* 16 kHz mono. RX: ADC DMA ring fed from the module AF_OUT. TX: DAC DMA ring to MIC_IN. */
size_t   hal_audio_rx_read(int16_t *pcm, size_t max_samples);
size_t   hal_audio_tx_write(const int16_t *pcm, size_t n_samples);
void     hal_audio_tx_enable(bool on);
void     hal_audio_local_path(bool speaker_on, bool mic_on);

/* --- power: charger (BQ25798) + PD controller (TPS25751) + gauge ---- */
typedef struct {
    uint16_t batt_mv, vbus_mv, sys_mv;
    int16_t  batt_ma, vbus_ma;
    int8_t   temp_c;
    uint8_t  soc_pct;
    bool     charging, sourcing, fault;
} hal_power_status_t;

void     hal_power_read(hal_power_status_t *st);
void     hal_power_set_sink_limit_ma(uint32_t ma);
void     hal_power_source_enable(bool on, uint8_t max_w);
void     hal_pd_request_pr_swap(void);
void     hal_pd_request_dr_swap(void);
void     hal_pd_reply_pr_swap(bool accept);
/* Called from the PD controller interrupt/poll with decoded events. */
struct pd_event; /* see pd_policy.h */

/* --- misc ------------------------------------------------------------ */
bool     hal_button_ptt(void);
void     hal_led(uint8_t idx, bool on);
int8_t   hal_pa_temp_c(void);
void     hal_log(const char *fmt, ...);

#endif
