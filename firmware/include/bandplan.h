/*
 * Band plan and operating mode.
 *
 *   PMR446   : 16 fixed channels, 12.5 kHz, ERP cap, anyone may use it.
 *   HAM_70CM : 430.000-440.000 MHz, 12.5 kHz grid, 1 W allowed, only after a
 *              valid amateur callsign has been stored (the licence holder
 *              takes responsibility; the firmware only gates the feature).
 */
#ifndef BANDPLAN_H
#define BANDPLAN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BAND_MODE_PMR446 = 0,
    BAND_MODE_HAM_70CM = 1,
} band_mode_t;

#define HAM_70CM_LOW_HZ     430000000u
#define HAM_70CM_HIGH_HZ    440000000u
#define HAM_70CM_STEP_HZ    12500u
#define HAM_APRS_HZ         432500000u   /* IARU R1 70 cm APRS */
#define LORA_APRS_433_HZ    433775000u
#define CALLSIGN_MAX        7            /* without SSID */

typedef struct {
    band_mode_t mode;
    char    callsign[CALLSIGN_MAX + 1];  /* upper case, NUL terminated */
    uint8_t ssid;                        /* 0..15 */
    char    nickname[CALLSIGN_MAX + 1];  /* PMR-mode identity for messaging */
    bool    unlocked;                    /* ham mode available */
} bandplan_t;

void bandplan_init(bandplan_t *b);

/* Amateur callsign syntax check: 3..7 upper-case alphanumerics, at least one
 * digit, at least one letter, last character a letter (e.g. SQ6EMM, DL1ABC,
 * 4X4AAA, VK2XYZ). Not a licence database lookup. */
bool bandplan_callsign_valid(const char *call);

/* Store identity; a valid callsign unlocks HAM mode. ssid > 15 is rejected. */
bool bandplan_set_callsign(bandplan_t *b, const char *call, uint8_t ssid);
bool bandplan_set_nickname(bandplan_t *b, const char *nick);  /* 1..7 printable, no spaces */

/* Switch mode; HAM requires unlocked. Returns false and leaves mode unchanged on refusal. */
bool bandplan_set_mode(bandplan_t *b, band_mode_t mode);

/* Frequency legality in the current mode. */
bool bandplan_valid_hz(const bandplan_t *b, uint32_t hz);

/* Nearest legal frequency in the current mode (clamped to the band). */
uint32_t bandplan_round_hz(const bandplan_t *b, uint32_t hz);

/* Default frequency when entering a mode (PMR ch1 / 433.500 FM calling... 433.500 is
 * the IARU R1 FM calling channel; we use 433.500 MHz). */
uint32_t bandplan_default_hz(band_mode_t mode);

/* Is the module's 1 W setting allowed? PMR: only if ERP stays <= 0.45 W given the
 * stored antenna gain; HAM: always. erp_dbm_x10_at_1w = 300 - loss + gain, tenths of dB. */
bool bandplan_high_power_allowed(const bandplan_t *b, int erp_dbm_x10_at_1w);

/* Identity string used in AX.25/APRS (callsign in HAM mode, nickname in PMR). */
const char *bandplan_identity(const bandplan_t *b);

#endif
