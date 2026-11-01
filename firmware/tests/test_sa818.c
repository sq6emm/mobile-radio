#include "sa818.h"
#include "test.h"

int main(void)
{
    char b[80];
    CHECK(sa818_cmd_connect(b, sizeof b) > 0); CHECK_STR(b, "AT+DMOCONNECT\r\n");

    sa818_group_t g = { 0, 446006250u, 446006250u, 0, 0, 4 };
    CHECK(sa818_cmd_setgroup(b, sizeof b, &g) > 0);
    CHECK_STR(b, "AT+DMOSETGROUP=0,446.0063,446.0063,0000,4,0000\r\n");
    g.ctcss_tx = 12; g.ctcss_rx = 38; g.tx_hz = g.rx_hz = 446193750u; g.squelch = 8;
    CHECK(sa818_cmd_setgroup(b, sizeof b, &g) > 0);
    CHECK_STR(b, "AT+DMOSETGROUP=0,446.1938,446.1938,0012,8,0038\r\n");
    g.squelch = 9; CHECK_EQ(sa818_cmd_setgroup(b, sizeof b, &g), 0);
    g.squelch = 1; g.ctcss_tx = 39; CHECK_EQ(sa818_cmd_setgroup(b, sizeof b, &g), 0);
    CHECK_EQ(sa818_cmd_setgroup(b, 20, &(sa818_group_t){ 0, 446006250u, 446006250u, 0, 0, 1 }), 0);

    CHECK(sa818_cmd_volume(b, sizeof b, 5) > 0); CHECK_STR(b, "AT+DMOSETVOLUME=5\r\n");
    CHECK_EQ(sa818_cmd_volume(b, sizeof b, 0), 0);
    CHECK_EQ(sa818_cmd_volume(b, sizeof b, 9), 0);
    CHECK(sa818_cmd_filter(b, sizeof b, false, true, false) > 0); CHECK_STR(b, "AT+SETFILTER=0,1,0\r\n");
    CHECK(sa818_cmd_tail(b, sizeof b, true) > 0); CHECK_STR(b, "AT+SETTAIL=1\r\n");
    CHECK(sa818_cmd_rssi(b, sizeof b) > 0); CHECK_STR(b, "RSSI?\r\n");

    sa818_resp_t r;
    CHECK(sa818_parse_response("+DMOCONNECT:0\r\n", &r)); CHECK_EQ(r.kind, SA818_RESP_CONNECT); CHECK_EQ(r.code, 0);
    CHECK(sa818_parse_response("\r\n+DMOSETGROUP:1", &r)); CHECK_EQ(r.kind, SA818_RESP_SETGROUP); CHECK_EQ(r.code, 1);
    CHECK(sa818_parse_response("+DMOSETVOLUME:0", &r)); CHECK_EQ(r.kind, SA818_RESP_SETVOLUME);
    CHECK(sa818_parse_response("+DMOSETFILTER:0", &r)); CHECK_EQ(r.kind, SA818_RESP_SETFILTER);
    CHECK(sa818_parse_response("RSSI=080\r\n", &r)); CHECK_EQ(r.kind, SA818_RESP_RSSI); CHECK_EQ(r.rssi_raw, 80);
    CHECK(sa818_parse_response("+VERSION:SA818_V4.0\r\n", &r)); CHECK_EQ(r.kind, SA818_RESP_VERSION);
    CHECK_STR(r.version, "SA818_V4.0");
    CHECK(sa818_parse_response("+DMOERROR\r\n", &r)); CHECK_EQ(r.kind, SA818_RESP_ERROR);
    CHECK(!sa818_parse_response("garbage", &r));
    CHECK(!sa818_parse_response(NULL, &r));

    CHECK_EQ(sa818_rssi_to_dbm(0, 0.5f, -137.0f), -137);
    CHECK_EQ(sa818_rssi_to_dbm(80, 0.5f, -137.0f), -97);
    CHECK_EQ(sa818_rssi_to_dbm(300, 0.5f, -137.0f), -10);   /* clamped to 255 -> -9.5 -> -10 */
    TEST_END();
}
