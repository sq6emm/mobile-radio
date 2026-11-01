#include "aprs.h"
#include "test.h"
#include <limits.h>
#include <math.h>

int main(void)
{
    char out[APRS_MAX_INFO];
    aprs_pos_t p = { .lat = 51.108333, .lon = 17.0375, .sym_table = '/', .sym_code = '[',
                     .course_deg = -1, .speed_knots = -1, .alt_ft = INT_MIN, .comment = "mobile-radio" };
    CHECK(aprs_encode_position(out, sizeof out, &p) > 0);
    CHECK_STR(out, "!5106.50N/01702.25E[mobile-radio");
    p.lat = -33.8688; p.lon = -151.2093; p.course_deg = 88; p.speed_knots = 36; p.alt_ft = 120; p.comment = NULL;
    p.sym_code = '>';
    CHECK(aprs_encode_position(out, sizeof out, &p) > 0);
    CHECK_STR(out, "!3352.13S/15112.56W>088/036/A=000120");
    p.lat = 91; CHECK_EQ(aprs_encode_position(out, sizeof out, &p), 0);
    p.lat = 0; p.lon = 0; p.course_deg = -1; p.alt_ft = INT_MIN;
    CHECK(aprs_encode_position(out, sizeof out, &p) > 0);
    CHECK_STR(out, "!0000.00N/00000.00E>");
    /* minute rounding carry: 51.99999 deg -> 52 00.00 */
    p.lat = 51.999999; p.lon = 17.999999;
    CHECK(aprs_encode_position(out, sizeof out, &p) > 0);
    CHECK_STR(out, "!5200.00N/01800.00E>");

    aprs_packet_t d;
    CHECK(aprs_decode("!5106.50N/01702.25E[mobile-radio", 32, &d));
    CHECK_EQ(d.kind, APRS_KIND_POSITION);
    CHECK(fabs(d.lat - 51.108333) < 1e-4); CHECK(fabs(d.lon - 17.0375) < 1e-4);
    CHECK_EQ(d.sym_table, '/'); CHECK_EQ(d.sym_code, '[');
    CHECK_STR(d.comment, "mobile-radio");
    const char *ts = "@092345z4903.50N/07201.75W>088/036comment";
    CHECK(aprs_decode(ts, strlen(ts), &d));
    CHECK_EQ(d.kind, APRS_KIND_POSITION); CHECK_EQ(d.course_deg, 88); CHECK_EQ(d.speed_knots, 36);
    CHECK(fabs(d.lon + 72.029167) < 1e-4); CHECK_STR(d.comment, "comment");
    CHECK(!aprs_decode("!/5L!!<*e7>7P[", 14, &d));                 /* compressed: unsupported */

    CHECK(aprs_encode_message(out, sizeof out, "SQ6EMM-7", "Hello there", "12") > 0);
    CHECK_STR(out, ":SQ6EMM-7 :Hello there{12");
    CHECK(aprs_encode_message(out, sizeof out, "SKI-7", "no ack please", NULL) > 0);
    CHECK_STR(out, ":SKI-7    :no ack please");
    CHECK_EQ(aprs_encode_message(out, sizeof out, "SKI-7", "bad|char", NULL), 0);
    CHECK_EQ(aprs_encode_message(out, sizeof out, "TOOLONGADDRESSEE", "x", NULL), 0);
    char longtxt[80]; memset(longtxt, 'a', 68); longtxt[68] = 0;
    CHECK_EQ(aprs_encode_message(out, sizeof out, "SKI-7", longtxt, NULL), 0);
    CHECK(aprs_encode_ack(out, sizeof out, "SQ6EMM-7", "12", false) > 0);
    CHECK_STR(out, ":SQ6EMM-7 :ack12");
    CHECK(aprs_encode_ack(out, sizeof out, "SQ6EMM-7", "12", true) > 0);
    CHECK_STR(out, ":SQ6EMM-7 :rej12");

    const char *m = ":SQ6EMM-7 :Hello there{12";
    CHECK(aprs_decode(m, strlen(m), &d));
    CHECK_EQ(d.kind, APRS_KIND_MESSAGE); CHECK_STR(d.addressee, "SQ6EMM-7");
    CHECK_STR(d.text, "Hello there"); CHECK_STR(d.msg_id, "12");
    m = ":SKI-7    :ack12";
    CHECK(aprs_decode(m, strlen(m), &d)); CHECK_EQ(d.kind, APRS_KIND_ACK); CHECK_STR(d.msg_id, "12");
    m = ":SKI-7    :rej7";
    CHECK(aprs_decode(m, strlen(m), &d)); CHECK_EQ(d.kind, APRS_KIND_REJ); CHECK_STR(d.msg_id, "7");
    m = ":SKI-7    :acknowledged that{3";
    CHECK(aprs_decode(m, strlen(m), &d)); CHECK_EQ(d.kind, APRS_KIND_MESSAGE); CHECK_STR(d.msg_id, "3");
    CHECK(!aprs_decode(":SHORT", 6, &d));
    m = ">status text";
    CHECK(aprs_decode(m, strlen(m), &d)); CHECK_EQ(d.kind, APRS_KIND_STATUS); CHECK_STR(d.comment, "status text");
    m = "T#123,1,2";
    CHECK(aprs_decode(m, strlen(m), &d)); CHECK_EQ(d.kind, APRS_KIND_UNKNOWN);
    TEST_END();
}
