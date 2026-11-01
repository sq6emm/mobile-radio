#include "beacon.h"
#include <stdlib.h>
#include <string.h>

void beacon_default_cfg(beacon_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->enabled = false;
    c->smart = true;
    c->fixed_interval_s = 600;
    c->slow_rate_s = 1800;
    c->fast_rate_s = 60;
    c->low_speed_kmh = 5;
    c->high_speed_kmh = 90;
    c->min_turn_deg = 28;
    c->turn_slope = 255;
    c->min_turn_time_s = 15;
    c->min_interval_s = 30;
}

void beacon_init(beacon_t *b, const beacon_cfg_t *cfg)
{
    memset(b, 0, sizeof(*b));
    if (cfg) b->cfg = *cfg; else beacon_default_cfg(&b->cfg);
    b->last_course_deg = -1;
    b->pos.course_deg = -1;
}

void beacon_update_position(beacon_t *b, const beacon_pos_t *pos)
{
    b->pos = *pos;
}

uint32_t beacon_rate_s(const beacon_t *b)
{
    const beacon_cfg_t *c = &b->cfg;
    uint32_t r;
    if (!c->smart) r = c->fixed_interval_s;
    else if (b->pos.speed_kmh <= c->low_speed_kmh) r = c->slow_rate_s;
    else if (b->pos.speed_kmh >= c->high_speed_kmh) r = c->fast_rate_s;
    else r = c->fast_rate_s * c->high_speed_kmh / b->pos.speed_kmh;   /* SmartBeaconing formula */
    return r < c->min_interval_s ? c->min_interval_s : r;
}

static int angle_diff(int a, int b)
{
    int d = abs(a - b) % 360;
    return d > 180 ? 360 - d : d;
}

bool beacon_due(const beacon_t *b, uint32_t now_ms)
{
    const beacon_cfg_t *c = &b->cfg;
    if (!c->enabled || !b->pos.valid) return false;
    if (!b->ever_sent) return true;
    uint32_t since_s = (now_ms - b->last_tx_ms) / 1000u;
    /* corner pegging: allowed after min_turn_time even inside the rate floor */
    if (c->smart && b->pos.course_deg >= 0 && b->last_course_deg >= 0 &&
        b->pos.speed_kmh > c->low_speed_kmh && since_s >= c->min_turn_time_s) {
        int threshold = c->min_turn_deg + c->turn_slope / b->pos.speed_kmh;
        if (angle_diff(b->pos.course_deg, b->last_course_deg) > threshold) return true;
    }
    if (since_s < c->min_interval_s) return false;
    return since_s >= beacon_rate_s(b);
}

void beacon_mark_sent(beacon_t *b, uint32_t now_ms)
{
    b->last_tx_ms = now_ms;
    b->ever_sent = true;
    b->last_course_deg = b->pos.course_deg;
}
