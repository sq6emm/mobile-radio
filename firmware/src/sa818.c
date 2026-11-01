#include "sa818.h"
#include "pmr446.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int put(char *buf, size_t cap, const char *s)
{
    int n = snprintf(buf, cap, "%s", s);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

int sa818_cmd_connect(char *buf, size_t cap) { return put(buf, cap, "AT+DMOCONNECT\r\n"); }
int sa818_cmd_version(char *buf, size_t cap) { return put(buf, cap, "AT+VERSION\r\n"); }
int sa818_cmd_rssi(char *buf, size_t cap)    { return put(buf, cap, "RSSI?\r\n"); }

int sa818_cmd_setgroup(char *buf, size_t cap, const sa818_group_t *g)
{
    char tx[12], rx[12];
    if (!g || g->bandwidth > 1 || g->ctcss_tx > CTCSS_TONES || g->ctcss_rx > CTCSS_TONES || g->squelch > 8)
        return 0;
    if (!pmr446_format_mhz(tx, sizeof tx, g->tx_hz) || !pmr446_format_mhz(rx, sizeof rx, g->rx_hz))
        return 0;
    int n = snprintf(buf, cap, "AT+DMOSETGROUP=%u,%s,%s,%04u,%u,%04u\r\n",
                     g->bandwidth, tx, rx, g->ctcss_tx, g->squelch, g->ctcss_rx);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

int sa818_cmd_volume(char *buf, size_t cap, uint8_t level)
{
    if (level < 1 || level > 8)
        return 0;
    int n = snprintf(buf, cap, "AT+DMOSETVOLUME=%u\r\n", level);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

int sa818_cmd_filter(char *buf, size_t cap, bool bypass_emphasis, bool bypass_hpf, bool bypass_lpf)
{
    int n = snprintf(buf, cap, "AT+SETFILTER=%d,%d,%d\r\n", bypass_emphasis ? 1 : 0,
                     bypass_hpf ? 1 : 0, bypass_lpf ? 1 : 0);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

int sa818_cmd_tail(char *buf, size_t cap, bool on)
{
    int n = snprintf(buf, cap, "AT+SETTAIL=%d\r\n", on ? 1 : 0);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

static bool parse_code(const char *s, const char *prefix, int *code)
{
    size_t n = strlen(prefix);
    if (strncmp(s, prefix, n) != 0)
        return false;
    *code = atoi(s + n);
    return true;
}

bool sa818_parse_response(const char *line, sa818_resp_t *out)
{
    static const struct { const char *prefix; sa818_resp_kind_t kind; } set_cmds[] = {
        { "+DMOCONNECT:",   SA818_RESP_CONNECT },
        { "+DMOSETGROUP:",  SA818_RESP_SETGROUP },
        { "+DMOSETVOLUME:", SA818_RESP_SETVOLUME },
        { "+DMOSETFILTER:", SA818_RESP_SETFILTER },
        { "+DMOSETTAIL:",   SA818_RESP_SETTAIL },
    };
    if (!line || !out)
        return false;
    memset(out, 0, sizeof(*out));
    while (*line == '\r' || *line == '\n' || *line == ' ')
        line++;
    for (size_t i = 0; i < sizeof set_cmds / sizeof set_cmds[0]; i++) {
        if (parse_code(line, set_cmds[i].prefix, &out->code)) {
            out->kind = set_cmds[i].kind;
            return true;
        }
    }
    if (strncmp(line, "RSSI=", 5) == 0) {
        out->kind = SA818_RESP_RSSI;
        out->rssi_raw = atoi(line + 5);
        return true;
    }
    if (strncmp(line, "+VERSION:", 9) == 0) {
        out->kind = SA818_RESP_VERSION;
        size_t i = 0;
        const char *v = line + 9;
        while (v[i] && v[i] != '\r' && v[i] != '\n' && i < sizeof out->version - 1) {
            out->version[i] = v[i];
            i++;
        }
        out->version[i] = '\0';
        return true;
    }
    if (strncmp(line, "+DMOERROR", 9) == 0) {
        out->kind = SA818_RESP_ERROR;
        return true;
    }
    return false;
}

int sa818_rssi_to_dbm(int raw, float slope, float offset)
{
    if (raw < 0) raw = 0;
    if (raw > 255) raw = 255;
    float v = offset + slope * (float)raw;
    return (int)(v < 0 ? v - 0.5f : v + 0.5f);
}
