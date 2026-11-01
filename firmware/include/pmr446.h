/* PMR446 channel plan, CTCSS tone table and regional limits. */
#ifndef PMR446_H
#define PMR446_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PMR446_CHANNELS      16
#define PMR446_CH1_HZ        446006250u
#define PMR446_STEP_HZ       12500u
#define PMR446_BAND_LOW_HZ   446000000u
#define PMR446_BAND_HIGH_HZ  446200000u
#define PMR446_ERP_LIMIT_MW  500u
#define CTCSS_TONES          38

enum radio_region {
    REGION_EU_PMR446 = 0,   /* the only region enabled in this firmware */
};

/* Frequency of a PMR446 channel 1..16, or 0 if the channel is invalid. */
uint32_t pmr446_channel_hz(uint8_t channel);

/* Channel number 1..16 for a frequency on the PMR446 grid, 0 if not on it. */
uint8_t pmr446_channel_from_hz(uint32_t hz);

/* CTCSS tone frequency in tenths of Hz for index 1..38 (0 for index 0 / invalid). */
uint16_t ctcss_dhz(uint8_t index);

/* Validate a complete channel request (channel, tones, squelch, power, bandwidth). */
bool pmr446_validate(uint8_t channel, uint8_t bandwidth, uint8_t ctcss_tx,
                     uint8_t ctcss_rx, uint8_t squelch, uint8_t power);

/* Format a frequency as "MMM.FFFF" (MHz, 4 decimals, nearest 100 Hz) for the
 * SA818/DRA818 AT command set. Returns characters written, 0 on error. */
int pmr446_format_mhz(char *buf, size_t cap, uint32_t hz);

#endif
