/*
 * Host (Linux) stub implementation of hal.h for compile checks and desk
 * simulation: USB is a pair of named pipes, the radio UART is a pseudo
 * terminal, audio is silence, the battery is always 80 %.
 * Not a functional emulator - it exists so the application links and the
 * port surface stays honest.
 */
#include "hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint32_t t0;
uint32_t hal_millis(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    if (!t0) t0 = ms;
    return ms - t0;
}
void hal_delay_ms(uint32_t ms) { struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
void hal_watchdog_kick(void) {}

bool   hal_usb_connected(void) { return false; }
size_t hal_usb_read(uint8_t *buf, size_t cap) { (void)buf; (void)cap; return 0; }
size_t hal_usb_write(const uint8_t *buf, size_t len) { (void)buf; return len; }
void   hal_usb_reenumerate_with_uac(bool enable) { (void)enable; }

void   hal_radio_power(bool on) { (void)on; }
void   hal_radio_ptt(bool key) { (void)key; }
void   hal_radio_high_power(bool high) { (void)high; }
bool   hal_radio_squelch_open(void) { return false; }
size_t hal_radio_uart_write(const char *s, size_t len) { (void)s; return len; }
size_t hal_radio_uart_read_line(char *buf, size_t cap, uint32_t timeout_ms)
{
    (void)timeout_ms;
    return (size_t)snprintf(buf, cap, "+DMOCONNECT:0\r\n");
}

size_t hal_audio_rx_read(int16_t *pcm, size_t n) { (void)pcm; (void)n; return 0; }
size_t hal_audio_tx_write(const int16_t *pcm, size_t n) { (void)pcm; return n; }
void   hal_audio_tx_enable(bool on) { (void)on; }
void   hal_audio_local_path(bool spk, bool mic) { (void)spk; (void)mic; }

void hal_power_read(hal_power_status_t *st)
{
    memset(st, 0, sizeof(*st));
    st->batt_mv = 7600; st->sys_mv = 7600; st->soc_pct = 80; st->temp_c = 25;
}
void hal_power_set_sink_limit_ma(uint32_t ma) { (void)ma; }
void hal_power_source_enable(bool on, uint8_t w) { (void)on; (void)w; }
void hal_pd_request_pr_swap(void) {}
void hal_pd_request_dr_swap(void) {}
void hal_pd_reply_pr_swap(bool a) { (void)a; }

bool   hal_button_ptt(void) { return false; }
void   hal_led(uint8_t i, bool on) { (void)i; (void)on; }
int8_t hal_pa_temp_c(void) { return 30; }
void   hal_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
