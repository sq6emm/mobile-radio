/*
 * Port layer: everything the application needs from the hardware.
 * Implemented once per target (port/stm32f405/, port/host/ for simulation).
 *
 * Audio runs at 48 kHz in hardware (HAL_AUDIO_RATE); the application
 * resamples to 16 kHz for voice. In the sound-card USB profile the port layer
 * couples the UAC streams directly to the ADC/DAC rings (hal_audio_usb_direct).
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_AUDIO_RATE 48000

/* --- time ------------------------------------------------------------ */
uint32_t hal_millis(void);
void     hal_delay_ms(uint32_t ms);
void     hal_watchdog_kick(void);

/* --- USB -------------------------------------------------------------- */
bool     hal_usb_connected(void);
size_t   hal_usb_read(uint8_t *buf, size_t cap);            /* vendor bulk, non-blocking */
size_t   hal_usb_write(const uint8_t *buf, size_t len);     /* vendor bulk, returns bytes queued */
void     hal_usb_set_profile(uint8_t profile);              /* 0 app, 1 sound-card; soft re-enumeration */
bool     hal_cdc_rts(void);                                 /* CDC RTS line state (PTT in sound-card profile) */
void     hal_audio_usb_direct(bool on);                     /* UAC <-> ADC/DAC coupling in the port layer */

/* --- radio module (SA818) ------------------------------------------- */
void     hal_radio_power(bool on);                          /* PD pin */
void     hal_radio_ptt(bool key);                           /* PTT pin, active low on the module */
void     hal_radio_high_power(bool high);                   /* H/L pin */
bool     hal_radio_squelch_open(void);                      /* SQ pin, low = carrier */
size_t   hal_radio_uart_write(const char *s, size_t len);
size_t   hal_radio_uart_read_line(char *buf, size_t cap, uint32_t timeout_ms);

/* --- RF switch: helix to FM chain (false) or to LoRa 433 module (true) --- */
void     hal_rf_antenna_to_lora433(bool to_lora);

/* --- LoRa modules: idx 0 = 433 MHz, 1 = 868 MHz ---------------------- */
void     hal_lora_spi_xfer(int idx, const uint8_t *tx, uint8_t *rx, size_t n);
void     hal_lora_wait_busy(int idx);
void     hal_lora_reset(int idx);
bool     hal_lora_dio1(int idx);
void     hal_lora_rf_switch(int idx, bool tx, bool rx);

/* --- audio (48 kHz mono s16) ----------------------------------------- */
size_t   hal_audio_rx_read(int16_t *pcm, size_t max_samples);   /* from the ADC ring */
size_t   hal_audio_tx_write(const int16_t *pcm, size_t n);      /* into the DAC ring */
size_t   hal_audio_tx_free(void);                               /* samples of room in the DAC ring */
size_t   hal_audio_tx_pending(void);                            /* samples not yet played */
void     hal_audio_tx_enable(bool on);
void     hal_audio_flat_path(bool flat);                        /* bypass the 300 Hz HPF (data modes) */
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

/* --- persistent settings (small key/value blob in flash/EEPROM) ------ */
bool     hal_settings_load(void *buf, size_t len);
bool     hal_settings_save(const void *buf, size_t len);

/* --- misc ------------------------------------------------------------ */
bool     hal_button_ptt(void);
bool     hal_button_mode_long(void);                         /* long press: toggle USB profile */
void     hal_led(uint8_t idx, bool on);
int8_t   hal_pa_temp_c(void);
void     hal_log(const char *fmt, ...);

#endif
