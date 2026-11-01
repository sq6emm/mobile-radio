#include "aprs.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fmt_lat(char *out, double lat)
{
    char h = lat >= 0 ? 'N' : 'S';
    lat = fabs(lat);
    int deg = (int)lat;
    double min = (lat - deg) * 60.0;
    if (min >= 59.995) { min = 0.0; deg++; }
    return sprintf(out, "%02d%05.2f%c", deg, min, h);
}

static int fmt_lon(char *out, double lon)
{
    char h = lon >= 0 ? 'E' : 'W';
    lon = fabs(lon);
    int deg = (int)lon;
    double min = (lon - deg) * 60.0;
    if (min >= 59.995) { min = 0.0; deg++; }
    return sprintf(out, "%03d%05.2f%c", deg, min, h);
}

int aprs_encode_position(char *out, size_t cap, const aprs_pos_t *p)
{
    if (!out || !p || fabs(p->lat) > 90.0 || fabs(p->lon) > 180.0) return 0;
    char buf[APRS_MAX_INFO];
    int n = 0;
    buf[n++] = '!';
    n += fmt_lat(buf + n, p->lat);
    buf[n++] = p->sym_table ? p->sym_table : '/';
    n += fmt_lon(buf + n, p->lon);
    buf[n++] = p->sym_code ? p->sym_code : '[';
    if (p->course_deg >= 0 && p->speed_knots >= 0)
        n += sprintf(buf + n, "%03d/%03d", p->course_deg % 360, p->speed_knots > 999 ? 999 : p->speed_knots);
    if (p->alt_ft != INT_MIN)
        n += sprintf(buf + n, "/A=%06d", p->alt_ft < -99999 ? -99999 : (p->alt_ft > 999999 ? 999999 : p->alt_ft));
    if (p->comment && *p->comment) {
        size_t room = sizeof buf - (size_t)n - 1;
        size_t cl = strlen(p->comment);
        if (cl > 43) cl = 43;
        if (cl > room) cl = room;
        memcpy(buf + n, p->comment, cl);
        n += (int)cl;
    }
    buf[n] = '\0';
    if ((size_t)n + 1 > cap) return 0;
    memcpy(out, buf, (size_t)n + 1);
    return n;
}

static bool valid_addressee(const char *a)
{
    size_t n = a ? strlen(a) : 0;
    if (n < 1 || n > APRS_ADDRESSEE) return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)a[i]) && a[i] != '-') return false;
    return true;
}

int aprs_encode_message(char *out, size_t cap, const char *addressee, const char *text, const char *msg_id)
{
    if (!valid_addressee(addressee) || !text) return 0;
    size_t tl = strlen(text);
    if (tl > APRS_MAX_TEXT) return 0;
    for (size_t i = 0; i < tl; i++)
        if (text[i] == '|' || text[i] == '~' || text[i] == '{' || (unsigned char)text[i] < 0x20) return 0;
    char idpart[8] = "";
    if (msg_id && *msg_id) {
        if (strlen(msg_id) > 5) return 0;
        snprintf(idpart, sizeof idpart, "{%s", msg_id);
    }
    int n = snprintf(out, cap, ":%-9s:%s%s", addressee, text, idpart);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

int aprs_encode_ack(char *out, size_t cap, const char *addressee, const char *msg_id, bool reject)
{
    if (!valid_addressee(addressee) || !msg_id || !*msg_id || strlen(msg_id) > 5) return 0;
    int n = snprintf(out, cap, ":%-9s:%s%s", addressee, reject ? "rej" : "ack", msg_id);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

static bool parse_latlon(const char *s, size_t len, double *lat, double *lon, char *table, char *code)
{
    /* DDMM.hhN T DDDMM.hhE C  = 8 + 1 + 9 + 1 = 19 chars */
    if (len < 19) return false;
    for (int i = 0; i < 7; i++) if (i != 4 && !isdigit((unsigned char)s[i]) && s[i] != ' ') return false;
    if (s[4] != '.') return false;
    char ns = s[7];
    if (ns != 'N' && ns != 'S') return false;
    double d = atof((char[]){ s[0], s[1], 0 });
    double m = atof((char[]){ s[2], s[3], s[4], s[5], s[6], 0 });
    *lat = (d + m / 60.0) * (ns == 'S' ? -1 : 1);
    *table = s[8];
    const char *t = s + 9;
    for (int i = 0; i < 8; i++) if (i != 5 && !isdigit((unsigned char)t[i]) && t[i] != ' ') return false;
    if (t[5] != '.') return false;
    char ew = t[8];
    if (ew != 'E' && ew != 'W') return false;
    d = atof((char[]){ t[0], t[1], t[2], 0 });
    m = atof((char[]){ t[3], t[4], t[5], t[6], t[7], 0 });
    *lon = (d + m / 60.0) * (ew == 'W' ? -1 : 1);
    *code = t[9];
    return fabs(*lat) <= 90.0 && fabs(*lon) <= 180.0;
}

bool aprs_decode(const char *info, size_t len, aprs_packet_t *out)
{
    if (!info || !out || len == 0) return false;
    memset(out, 0, sizeof(*out));
    out->course_deg = out->speed_knots = -1;
    char dti = info[0];

    if (dti == ':' ) {
        if (len < 11 || info[10] != ':') return false;
        int n = 0;
        for (int i = 1; i < 10; i++) if (info[i] != ' ') out->addressee[n++] = info[i]; else break;
        out->addressee[n] = '\0';
        const char *body = info + 11;
        size_t bl = len - 11;
        if (bl >= 4 && (strncmp(body, "ack", 3) == 0 || strncmp(body, "rej", 3) == 0) && bl <= 8) {
            out->kind = body[0] == 'a' ? APRS_KIND_ACK : APRS_KIND_REJ;
            size_t il = bl - 3; if (il > 5) il = 5;
            memcpy(out->msg_id, body + 3, il); out->msg_id[il] = '\0';
            return true;
        }
        out->kind = APRS_KIND_MESSAGE;
        const char *brace = memchr(body, '{', bl);
        size_t tl = brace ? (size_t)(brace - body) : bl;
        if (tl > APRS_MAX_TEXT) tl = APRS_MAX_TEXT;
        memcpy(out->text, body, tl); out->text[tl] = '\0';
        if (brace) {
            size_t il = bl - tl - 1; if (il > 5) il = 5;
            memcpy(out->msg_id, brace + 1, il); out->msg_id[il] = '\0';
        }
        return true;
    }

    if (dti == '!' || dti == '=' || dti == '/' || dti == '@') {
        size_t off = 1;
        if (dti == '/' || dti == '@') off += 7;     /* timestamp DDHHMMz / HHMMSSh */
        if (len < off + 19) return false;
        if (!parse_latlon(info + off, len - off, &out->lat, &out->lon, &out->sym_table, &out->sym_code))
            return false;                            /* compressed positions not supported */
        out->kind = APRS_KIND_POSITION;
        size_t p = off + 19;
        if (len >= p + 7 && info[p + 3] == '/' && isdigit((unsigned char)info[p]) && isdigit((unsigned char)info[p + 4])) {
            out->course_deg = atoi((char[]){ info[p], info[p + 1], info[p + 2], 0 });
            out->speed_knots = atoi((char[]){ info[p + 4], info[p + 5], info[p + 6], 0 });
            p += 7;
        }
        size_t cl = len - p; if (cl >= sizeof out->comment) cl = sizeof out->comment - 1;
        memcpy(out->comment, info + p, cl); out->comment[cl] = '\0';
        return true;
    }

    if (dti == '>') {
        out->kind = APRS_KIND_STATUS;
        size_t cl = len - 1; if (cl >= sizeof out->comment) cl = sizeof out->comment - 1;
        memcpy(out->comment, info + 1, cl); out->comment[cl] = '\0';
        return true;
    }
    out->kind = APRS_KIND_UNKNOWN;
    return true;
}
