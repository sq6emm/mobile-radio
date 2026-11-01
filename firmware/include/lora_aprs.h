/*
 * LoRa-APRS frame format (as used by the common 433.775 MHz LoRa APRS
 * iGates and trackers): three header bytes '<' 0xFF 0x01 followed by a
 * TNC2-style text frame "SRC>DST,PATH:INFO". The INFO field is ordinary
 * APRS (aprs.h), so positions and messages interoperate with the APRS-IS.
 */
#ifndef LORA_APRS_H
#define LORA_APRS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LORA_APRS_MAX 255

typedef struct {
    char src[10];
    char dst[10];
    char path[32];      /* may be "" */
    const uint8_t *info;
    size_t info_len;
} lora_aprs_pkt_t;

size_t lora_aprs_encode(uint8_t *out, size_t cap, const char *src, const char *dst,
                        const char *path, const char *info, size_t info_len);
bool   lora_aprs_decode(const uint8_t *in, size_t len, lora_aprs_pkt_t *out);

#endif
