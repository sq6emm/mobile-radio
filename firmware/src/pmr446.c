#include "pmr446.h"
#include <stdio.h>

/* EIA/TIA-603 standard 38-tone CTCSS set, index 1..38, tenths of Hz.
 * This is the numbering used by the SA818/DRA818 modules. */
static const uint16_t ctcss_table[CTCSS_TONES + 1] = {
       0,
     670,  719,  744,  770,  797,  825,  854,  885,  915,  948,
     974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273, 1318,
    1365, 1413, 1462, 1514, 1567, 1622, 1679, 1738, 1799, 1862,
    1928, 2035, 2107, 2181, 2257, 2336, 2418, 2503,
};

uint32_t pmr446_channel_hz(uint8_t channel)
{
    if (channel < 1 || channel > PMR446_CHANNELS)
        return 0;
    return PMR446_CH1_HZ + (uint32_t)(channel - 1) * PMR446_STEP_HZ;
}

uint8_t pmr446_channel_from_hz(uint32_t hz)
{
    if (hz < PMR446_CH1_HZ)
        return 0;
    uint32_t off = hz - PMR446_CH1_HZ;
    if (off % PMR446_STEP_HZ)
        return 0;
    uint32_t idx = off / PMR446_STEP_HZ;
    return (idx < PMR446_CHANNELS) ? (uint8_t)(idx + 1) : 0;
}

uint16_t ctcss_dhz(uint8_t index)
{
    return (index <= CTCSS_TONES) ? ctcss_table[index] : 0;
}

bool pmr446_validate(uint8_t channel, uint8_t bandwidth, uint8_t ctcss_tx,
                     uint8_t ctcss_rx, uint8_t squelch, uint8_t power)
{
    if (pmr446_channel_hz(channel) == 0) return false;
    if (bandwidth != 0) return false;             /* 12.5 kHz only */
    if (ctcss_tx > CTCSS_TONES || ctcss_rx > CTCSS_TONES) return false;
    if (squelch > 8) return false;
    if (power > 1) return false;
    return true;
}

int pmr446_format_mhz(char *buf, size_t cap, uint32_t hz)
{
    if (!buf || cap < 9)
        return 0;
    uint32_t mhz = hz / 1000000u;
    uint32_t frac_100hz = (hz % 1000000u + 50u) / 100u;   /* round to nearest 100 Hz */
    if (frac_100hz >= 10000u) {                             /* carried into the MHz digit */
        frac_100hz -= 10000u;
        mhz += 1u;
    }
    int n = snprintf(buf, cap, "%03lu.%04lu", (unsigned long)mhz, (unsigned long)frac_100hz);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}
