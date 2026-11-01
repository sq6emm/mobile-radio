#include "lora_aprs.h"
#include <string.h>

size_t lora_aprs_encode(uint8_t *out, size_t cap, const char *src, const char *dst,
                        const char *path, const char *info, size_t info_len)
{
    if (!out || !src || !dst || !info) return 0;
    size_t sl = strlen(src), dl = strlen(dst), pl = path ? strlen(path) : 0;
    if (sl < 1 || sl > 9 || dl < 1 || dl > 9 || pl > 31) return 0;
    size_t n = 3 + sl + 1 + dl + (pl ? 1 + pl : 0) + 1 + info_len;
    if (n > cap || n > LORA_APRS_MAX) return 0;
    uint8_t *p = out;
    *p++ = '<'; *p++ = 0xFF; *p++ = 0x01;
    memcpy(p, src, sl); p += sl;
    *p++ = '>';
    memcpy(p, dst, dl); p += dl;
    if (pl) { *p++ = ','; memcpy(p, path, pl); p += pl; }
    *p++ = ':';
    memcpy(p, info, info_len);
    return n;
}

bool lora_aprs_decode(const uint8_t *in, size_t len, lora_aprs_pkt_t *out)
{
    if (!in || !out || len < 3 + 1 + 1 + 1 + 1 || in[0] != '<' || in[1] != 0xFF || in[2] != 0x01) return false;
    memset(out, 0, sizeof(*out));
    const uint8_t *p = in + 3, *end = in + len;
    const uint8_t *gt = memchr(p, '>', (size_t)(end - p));
    if (!gt || gt == p || gt - p > 9) return false;
    memcpy(out->src, p, (size_t)(gt - p));
    p = gt + 1;
    const uint8_t *colon = memchr(p, ':', (size_t)(end - p));
    if (!colon || colon == p) return false;
    const uint8_t *comma = memchr(p, ',', (size_t)(colon - p));
    size_t dl = (size_t)((comma ? comma : colon) - p);
    if (dl < 1 || dl > 9) return false;
    memcpy(out->dst, p, dl);
    if (comma) {
        size_t pl = (size_t)(colon - comma - 1);
        if (pl > 31) return false;
        memcpy(out->path, comma + 1, pl);
    }
    out->info = colon + 1;
    out->info_len = (size_t)(end - colon - 1);
    return true;
}
