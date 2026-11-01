/*
 * APRS position beacon scheduler: fixed interval or SmartBeaconing
 * (rate follows speed, extra beacon on course change), with a hard
 * minimum interval as the duty-cycle guard.
 */
#ifndef BEACON_H
#define BEACON_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     enabled;
    bool     smart;
    uint32_t fixed_interval_s;   /* used when !smart (default 600) */
    /* SmartBeaconing */
    uint32_t slow_rate_s;        /* at or below low_speed (default 1800) */
    uint32_t fast_rate_s;        /* at or above high_speed (default 60) */
    uint16_t low_speed_kmh;      /* default 5 */
    uint16_t high_speed_kmh;     /* default 90 */
    uint16_t min_turn_deg;       /* default 28 */
    uint16_t turn_slope;         /* default 255 (deg*kmh) */
    uint32_t min_turn_time_s;    /* default 15 */
    uint32_t min_interval_s;     /* absolute floor (default 30) */
} beacon_cfg_t;

typedef struct {
    double   lat, lon;
    int      alt_m;              /* INT32_MIN = unknown */
    uint16_t speed_kmh;
    int16_t  course_deg;         /* -1 = unknown */
    bool     valid;
} beacon_pos_t;

typedef struct {
    beacon_cfg_t cfg;
    beacon_pos_t pos;
    uint32_t     last_tx_ms;
    bool         ever_sent;
    int16_t      last_course_deg;
} beacon_t;

void beacon_default_cfg(beacon_cfg_t *cfg);
void beacon_init(beacon_t *b, const beacon_cfg_t *cfg);
void beacon_update_position(beacon_t *b, const beacon_pos_t *pos);
/* Seconds between beacons at the current speed (smart) or the fixed interval. */
uint32_t beacon_rate_s(const beacon_t *b);
bool beacon_due(const beacon_t *b, uint32_t now_ms);
void beacon_mark_sent(beacon_t *b, uint32_t now_ms);

#endif
