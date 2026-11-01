#include "pmr446.h"
#include "test.h"

int main(void)
{
    CHECK_EQ(pmr446_channel_hz(1), 446006250u);
    CHECK_EQ(pmr446_channel_hz(8), 446093750u);
    CHECK_EQ(pmr446_channel_hz(16), 446193750u);
    CHECK_EQ(pmr446_channel_hz(0), 0);
    CHECK_EQ(pmr446_channel_hz(17), 0);
    for (uint8_t ch = 1; ch <= 16; ch++) {
        uint32_t hz = pmr446_channel_hz(ch);
        CHECK(hz > PMR446_BAND_LOW_HZ && hz < PMR446_BAND_HIGH_HZ);
        CHECK_EQ(pmr446_channel_from_hz(hz), ch);
    }
    CHECK_EQ(pmr446_channel_from_hz(446000000u), 0);
    CHECK_EQ(pmr446_channel_from_hz(446206250u), 0);
    CHECK_EQ(pmr446_channel_from_hz(433920000u), 0);

    CHECK_EQ(ctcss_dhz(0), 0);
    CHECK_EQ(ctcss_dhz(1), 670);
    CHECK_EQ(ctcss_dhz(12), 1000);
    CHECK_EQ(ctcss_dhz(38), 2503);
    CHECK_EQ(ctcss_dhz(39), 0);
    for (uint8_t i = 2; i <= 38; i++) CHECK(ctcss_dhz(i) > ctcss_dhz(i - 1));

    CHECK(pmr446_validate(1, 0, 0, 0, 4, 0));
    CHECK(pmr446_validate(16, 0, 38, 38, 8, 1));
    CHECK(!pmr446_validate(17, 0, 0, 0, 4, 0));
    CHECK(!pmr446_validate(1, 1, 0, 0, 4, 0));    /* 25 kHz not legal on PMR446 */
    CHECK(!pmr446_validate(1, 0, 39, 0, 4, 0));
    CHECK(!pmr446_validate(1, 0, 0, 0, 9, 0));
    CHECK(!pmr446_validate(1, 0, 0, 0, 4, 2));

    char b[12];
    CHECK(pmr446_format_mhz(b, sizeof b, 446006250u) > 0); CHECK_STR(b, "446.0063");
    CHECK(pmr446_format_mhz(b, sizeof b, 446193750u) > 0); CHECK_STR(b, "446.1938");
    CHECK(pmr446_format_mhz(b, sizeof b, 446100000u) > 0); CHECK_STR(b, "446.1000");
    CHECK(pmr446_format_mhz(b, sizeof b, 446999960u) > 0); CHECK_STR(b, "447.0000");
    CHECK_EQ(pmr446_format_mhz(b, 8, 446100000u), 0);
    TEST_END();
}
