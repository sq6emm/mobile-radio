/* APRS information-field encoding and decoding (subset: positions, messages, acks). */
#ifndef APRS_H
#define APRS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APRS_MAX_INFO 256
#define APRS_MAX_TEXT 67
#define APRS_ADDRESSEE 9

typedef struct {
    double lat, lon;          /* degrees, + = N / E */
    char   sym_table, sym_code;
    int    course_deg;        /* -1 = none */
    int    speed_knots;       /* -1 = none */
    int    alt_ft;            /* INT32_MIN = none */
    const char *comment;      /* may be NULL */
} aprs_pos_t;

typedef enum {
    APRS_KIND_UNKNOWN = 0,
    APRS_KIND_POSITION,
    APRS_KIND_MESSAGE,
    APRS_KIND_ACK,
    APRS_KIND_REJ,
    APRS_KIND_STATUS,
} aprs_kind_t;

typedef struct {
    aprs_kind_t kind;
    /* position */
    double lat, lon;
    char   sym_table, sym_code;
    int    course_deg, speed_knots;
    char   comment[APRS_MAX_INFO];
    /* message / ack */
    char   addressee[APRS_ADDRESSEE + 1];
    char   text[APRS_MAX_TEXT + 1];
    char   msg_id[6];          /* "" if none */
} aprs_packet_t;

/* "!DDMM.hhN/DDDMM.hhE$CSE/SPD/A=aaaaaa comment" (no timestamp). Returns length or 0. */
int aprs_encode_position(char *out, size_t cap, const aprs_pos_t *p);

/* ":ADDRESSEE:text{id}"; msg_id NULL or "" for no id (no ack requested). */
int aprs_encode_message(char *out, size_t cap, const char *addressee, const char *text, const char *msg_id);

/* ":ADDRESSEE:ackID" / ":ADDRESSEE:rejID" */
int aprs_encode_ack(char *out, size_t cap, const char *addressee, const char *msg_id, bool reject);

bool aprs_decode(const char *info, size_t len, aprs_packet_t *out);

#endif
