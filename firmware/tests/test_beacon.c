#include "beacon.h"
#include "test.h"
#include <limits.h>

int main(void)
{
    beacon_t b; beacon_init(&b, NULL);
    beacon_pos_t p = { .lat = 51.1, .lon = 17.0, .alt_m = INT_MIN, .speed_kmh = 0, .course_deg = -1, .valid = true };
    CHECK(!beacon_due(&b, 0));                 /* disabled */
    b.cfg.enabled = true;
    CHECK(!beacon_due(&b, 0));                 /* no position yet */
    beacon_update_position(&b, &p);
    CHECK(beacon_due(&b, 0));                  /* first beacon immediately */
    beacon_mark_sent(&b, 0);
    CHECK_EQ(beacon_rate_s(&b), 1800);         /* stationary: slow rate */
    CHECK(!beacon_due(&b, 1000 * 1000));
    CHECK(beacon_due(&b, 1800 * 1000));

    p.speed_kmh = 90; p.course_deg = 10; beacon_update_position(&b, &p);
    CHECK_EQ(beacon_rate_s(&b), 60);
    p.speed_kmh = 45; beacon_update_position(&b, &p);
    CHECK_EQ(beacon_rate_s(&b), 120);
    p.speed_kmh = 200; beacon_update_position(&b, &p);
    CHECK_EQ(beacon_rate_s(&b), 60);

    /* corner pegging: course change beyond threshold after min_turn_time */
    beacon_mark_sent(&b, 2000 * 1000);
    p.speed_kmh = 50; p.course_deg = 10; beacon_update_position(&b, &p);
    beacon_mark_sent(&b, 2000 * 1000);
    p.course_deg = 60; beacon_update_position(&b, &p);              /* 50 deg turn, threshold 28+5=33 */
    CHECK(!beacon_due(&b, 2000 * 1000 + 10 * 1000));                /* before min_turn_time */
    CHECK(beacon_due(&b, 2000 * 1000 + 16 * 1000));
    p.course_deg = 70; beacon_update_position(&b, &p);
    beacon_mark_sent(&b, 2100 * 1000);
    p.course_deg = 80; beacon_update_position(&b, &p);              /* 10 deg: no */
    CHECK(!beacon_due(&b, 2100 * 1000 + 20 * 1000));
    /* wrap-around angle: 350 -> 20 is 30 deg */
    p.course_deg = 350; beacon_update_position(&b, &p); beacon_mark_sent(&b, 2200 * 1000);
    p.course_deg = 30; beacon_update_position(&b, &p);              /* 40 deg */
    CHECK(beacon_due(&b, 2200 * 1000 + 20 * 1000));

    /* hard floor */
    b.cfg.smart = false; b.cfg.fixed_interval_s = 5;
    CHECK_EQ(beacon_rate_s(&b), 30);
    beacon_mark_sent(&b, 3000 * 1000);
    CHECK(!beacon_due(&b, 3000 * 1000 + 29 * 1000));
    CHECK(beacon_due(&b, 3000 * 1000 + 30 * 1000));
    TEST_END();
}
