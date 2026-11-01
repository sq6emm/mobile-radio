#include "bandplan.h"
#include "pmr446.h"
#include <ctype.h>
#include <string.h>

void bandplan_init(bandplan_t *b)
{
    memset(b, 0, sizeof(*b));
    b->mode = BAND_MODE_PMR446;
    strcpy(b->nickname, "PACK");
}

bool bandplan_callsign_valid(const char *call)
{
    if (!call) return false;
    size_t n = strlen(call);
    if (n < 3 || n > CALLSIGN_MAX) return false;
    bool digit = false, letter = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)call[i];
        if (isdigit(c)) digit = true;
        else if (isupper(c)) letter = true;
        else return false;
    }
    return digit && letter && isupper((unsigned char)call[n - 1]);
}

bool bandplan_set_callsign(bandplan_t *b, const char *call, uint8_t ssid)
{
    if (!bandplan_callsign_valid(call) || ssid > 15) return false;
    strncpy(b->callsign, call, CALLSIGN_MAX);
    b->callsign[CALLSIGN_MAX] = '\0';
    b->ssid = ssid;
    b->unlocked = true;
    return true;
}

bool bandplan_set_nickname(bandplan_t *b, const char *nick)
{
    if (!nick) return false;
    size_t n = strlen(nick);
    if (n < 1 || n > CALLSIGN_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)nick[i];
        if (!isalnum(c) && c != '-' && c != '_') return false;
    }
    strncpy(b->nickname, nick, CALLSIGN_MAX);
    b->nickname[CALLSIGN_MAX] = '\0';
    return true;
}

bool bandplan_set_mode(bandplan_t *b, band_mode_t mode)
{
    if (mode == BAND_MODE_HAM_70CM && !b->unlocked) return false;
    if (mode != BAND_MODE_PMR446 && mode != BAND_MODE_HAM_70CM) return false;
    b->mode = mode;
    return true;
}

bool bandplan_valid_hz(const bandplan_t *b, uint32_t hz)
{
    if (b->mode == BAND_MODE_PMR446)
        return pmr446_channel_from_hz(hz) != 0;
    if (hz < HAM_70CM_LOW_HZ || hz > HAM_70CM_HIGH_HZ) return false;
    return ((hz - HAM_70CM_LOW_HZ) % HAM_70CM_STEP_HZ) == 0;
}

uint32_t bandplan_round_hz(const bandplan_t *b, uint32_t hz)
{
    if (b->mode == BAND_MODE_PMR446) {
        uint32_t lo = PMR446_CH1_HZ, hi = pmr446_channel_hz(PMR446_CHANNELS);
        if (hz <= lo) return lo;
        if (hz >= hi) return hi;
        uint32_t idx = (hz - lo + PMR446_STEP_HZ / 2) / PMR446_STEP_HZ;
        return lo + idx * PMR446_STEP_HZ;
    }
    if (hz <= HAM_70CM_LOW_HZ) return HAM_70CM_LOW_HZ;
    if (hz >= HAM_70CM_HIGH_HZ) return HAM_70CM_HIGH_HZ;
    uint32_t idx = (hz - HAM_70CM_LOW_HZ + HAM_70CM_STEP_HZ / 2) / HAM_70CM_STEP_HZ;
    return HAM_70CM_LOW_HZ + idx * HAM_70CM_STEP_HZ;
}

uint32_t bandplan_default_hz(band_mode_t mode)
{
    return mode == BAND_MODE_HAM_70CM ? 433500000u : PMR446_CH1_HZ;
}

bool bandplan_high_power_allowed(const bandplan_t *b, int erp_dbm_x10_at_1w)
{
    if (b->mode == BAND_MODE_HAM_70CM) return true;
    return erp_dbm_x10_at_1w <= 265;   /* 26.5 dBm = 0.45 W ERP, 0.5 dB margin */
}

const char *bandplan_identity(const bandplan_t *b)
{
    return (b->mode == BAND_MODE_HAM_70CM && b->unlocked) ? b->callsign : b->nickname;
}
