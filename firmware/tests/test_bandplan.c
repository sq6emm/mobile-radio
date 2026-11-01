#include "bandplan.h"
#include "test.h"

int main(void)
{
    bandplan_t b;
    bandplan_init(&b);
    CHECK_EQ(b.mode, BAND_MODE_PMR446);
    CHECK(!bandplan_set_mode(&b, BAND_MODE_HAM_70CM));           /* locked */
    CHECK(bandplan_valid_hz(&b, 446006250u));
    CHECK(!bandplan_valid_hz(&b, 433500000u));
    CHECK_EQ(bandplan_round_hz(&b, 446010000u), 446006250u);
    CHECK_EQ(bandplan_round_hz(&b, 446013000u), 446018750u);
    CHECK_EQ(bandplan_round_hz(&b, 400000000u), 446006250u);
    CHECK_EQ(bandplan_round_hz(&b, 470000000u), 446193750u);
    CHECK_STR(bandplan_identity(&b), "PACK");
    CHECK(bandplan_set_nickname(&b, "SKI-7"));
    CHECK(!bandplan_set_nickname(&b, "TOO LONG!"));
    CHECK(!bandplan_set_nickname(&b, ""));
    CHECK_STR(bandplan_identity(&b), "SKI-7");

    CHECK(bandplan_callsign_valid("SQ6EMM"));
    CHECK(bandplan_callsign_valid("DL1ABC"));
    CHECK(bandplan_callsign_valid("4X4AAA"));
    CHECK(bandplan_callsign_valid("K1A"));
    CHECK(!bandplan_callsign_valid("sq6emm"));
    CHECK(!bandplan_callsign_valid("ABCDEF"));
    CHECK(!bandplan_callsign_valid("123456"));
    CHECK(!bandplan_callsign_valid("SQ6EM1"));
    CHECK(!bandplan_callsign_valid("SQ6EMMXX"));
    CHECK(!bandplan_callsign_valid("SQ"));
    CHECK(!bandplan_set_callsign(&b, "SQ6EMM", 16));
    CHECK(!b.unlocked);
    CHECK(bandplan_set_callsign(&b, "SQ6EMM", 7));
    CHECK(b.unlocked);
    CHECK(bandplan_set_mode(&b, BAND_MODE_HAM_70CM));
    CHECK_STR(bandplan_identity(&b), "SQ6EMM");
    CHECK(bandplan_valid_hz(&b, 430000000u));
    CHECK(bandplan_valid_hz(&b, 440000000u));
    CHECK(bandplan_valid_hz(&b, 432500000u));
    CHECK(bandplan_valid_hz(&b, 433775000u));
    CHECK(!bandplan_valid_hz(&b, 429987500u));
    CHECK(!bandplan_valid_hz(&b, 440012500u));
    CHECK(!bandplan_valid_hz(&b, 433506250u));
    CHECK(!bandplan_valid_hz(&b, 446006250u));                   /* PMR grid is off the 12.5 grid from 430 */
    CHECK_EQ(bandplan_round_hz(&b, 433506000u), 433500000u);
    CHECK_EQ(bandplan_round_hz(&b, 445000000u), 440000000u);
    CHECK_EQ(bandplan_default_hz(BAND_MODE_HAM_70CM), 433500000u);
    CHECK_EQ(bandplan_default_hz(BAND_MODE_PMR446), 446006250u);
    CHECK(bandplan_high_power_allowed(&b, 300));                 /* ham: always */
    CHECK(bandplan_set_mode(&b, BAND_MODE_PMR446));
    CHECK(!bandplan_high_power_allowed(&b, 289 - 11 + 10));       /* ERP 28.8 dBm: no */
    CHECK(bandplan_high_power_allowed(&b, 300 - 11 - 40));        /* -4 dBd antenna: 24.9 dBm: yes */
    CHECK(!bandplan_set_mode(&b, (band_mode_t)5));
    TEST_END();
}
