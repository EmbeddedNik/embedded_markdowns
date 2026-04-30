/*
 * @file    proto_utils.c
 * @brief   Protocol frame checksum (CRC-8, poly 0x07) and byte-by-byte parser.
 *          No FreeRTOS or ESP-IDF dependencies — host-testable.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#include "proto_utils.h"

static uint8_t crc8_byte(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
    }
    return crc;
}

uint8_t calc_checksum(uint8_t msg_type, uint8_t payload_len, const uint8_t *payload)
{
    uint8_t crc = crc8_byte(0x00u, msg_type);
    crc = crc8_byte(crc, payload_len);
    for (uint8_t i = 0; i < payload_len; i++) {
        crc = crc8_byte(crc, payload[i]);
    }
    return crc;
}

parse_result_t rx_parse_byte(rx_parser_t *rx, uint8_t byte)
{
    switch (rx->state) {
        case RX_WAIT_START:
            if (byte == PROTO_START_BYTE) {
                rx->state = RX_MSG_TYPE;
            }
            break;

        case RX_MSG_TYPE:
            rx->msg_type = byte;
            rx->state    = RX_PAYLOAD_LEN;
            break;

        case RX_PAYLOAD_LEN:
            if (byte > PROTO_MAX_PAYLOAD_LEN) {
                rx->state = RX_WAIT_START;
                return PARSE_ERROR;
            }
            rx->payload_len = byte;
            rx->payload_idx = 0;
            rx->state       = (byte > 0) ? RX_PAYLOAD : RX_CHECKSUM;
            break;

        case RX_PAYLOAD:
            rx->payload[rx->payload_idx++] = byte;
            if (rx->payload_idx >= rx->payload_len) {
                rx->state = RX_CHECKSUM;
            }
            break;

        case RX_CHECKSUM: {
            uint8_t expected = calc_checksum(rx->msg_type, rx->payload_len, rx->payload);
            rx->state = RX_WAIT_START;
            if (byte != expected) {
                return PARSE_ERROR;
            }
            return PARSE_COMPLETE;
        }
    }
    return PARSE_INCOMPLETE;
}
