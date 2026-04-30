/*
 * @file    proto_utils.h
 * @brief   Protocol frame checksum and byte-by-byte parser.
 *          No FreeRTOS or ESP-IDF dependencies — host-testable.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#ifndef PROTO_UTILS_H
#define PROTO_UTILS_H

#include <stdint.h>
#include "protocol.h"

/* Return value of rx_parse_byte() */
typedef enum {
    PARSE_INCOMPLETE = 0,   /* frame not yet complete, keep feeding bytes */
    PARSE_COMPLETE,         /* full valid frame received; data in rx_parser_t */
    PARSE_ERROR,            /* checksum mismatch or oversized payload; resynced */
} parse_result_t;

typedef enum {
    RX_WAIT_START = 0,
    RX_MSG_TYPE,
    RX_PAYLOAD_LEN,
    RX_PAYLOAD,
    RX_CHECKSUM,
} rx_state_t;

typedef struct {
    rx_state_t state;
    uint8_t    msg_type;
    uint8_t    payload_len;
    uint8_t    payload[PROTO_MAX_PAYLOAD_LEN];
    uint8_t    payload_idx;
} rx_parser_t;

uint8_t        calc_checksum(uint8_t msg_type, uint8_t payload_len, const uint8_t *payload);
parse_result_t rx_parse_byte(rx_parser_t *rx, uint8_t byte);

#endif /* PROTO_UTILS_H */
