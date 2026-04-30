/*
 * @file    test_protocol.c
 * @brief   Unit tests for CRC-8 checksum and the UART frame parser.
 *          Run with: pio test -e native
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#include "unity.h"
#include "proto_utils.h"

void setUp(void)    {}
void tearDown(void) {}

/* ── Checksum ────────────────────────────────────────────────────── */

void test_checksum_is_deterministic(void)
{
    /* Same input must always produce the same CRC */
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t crc1 = calc_checksum(MSG_SENSOR_DATA, sizeof(payload), payload);
    uint8_t crc2 = calc_checksum(MSG_SENSOR_DATA, sizeof(payload), payload);
    TEST_ASSERT_EQUAL(crc1, crc2);
}

void test_checksum_changes_with_different_input(void)
{
    /* Different payloads must produce different CRCs */
    uint8_t payload_a[] = {0xAA};
    uint8_t payload_b[] = {0xBB};
    uint8_t crc_a = calc_checksum(MSG_SENSOR_DATA, 1, payload_a);
    uint8_t crc_b = calc_checksum(MSG_SENSOR_DATA, 1, payload_b);
    TEST_ASSERT_NOT_EQUAL(crc_a, crc_b);
}

/* ── Frame parser: happy path ────────────────────────────────────── */

void test_parser_accepts_valid_heartbeat_frame(void)
{
    /* Feed a correctly built heartbeat frame byte by byte.
     * MSG_HEARTBEAT has no payload, so the frame is 4 bytes:
     * [START | TYPE | LEN=0 | CHECKSUM] */
    rx_parser_t parser = {.state = RX_WAIT_START};
    uint8_t checksum = calc_checksum(MSG_HEARTBEAT, 0, NULL);

    uint8_t frame[] = {PROTO_START_BYTE, MSG_HEARTBEAT, 0x00, checksum};

    parse_result_t result = PARSE_INCOMPLETE;
    for (int i = 0; i < (int)sizeof(frame); i++) {
        result = rx_parse_byte(&parser, frame[i]);
    }

    TEST_ASSERT_EQUAL(PARSE_COMPLETE, result);
    TEST_ASSERT_EQUAL(MSG_HEARTBEAT, parser.msg_type);
    TEST_ASSERT_EQUAL(RX_WAIT_START, parser.state);  /* ready for next frame */
}

/* ── Frame parser: error handling ────────────────────────────────── */

void test_parser_rejects_wrong_checksum(void)
{
    /* A frame with a flipped checksum byte must be rejected */
    rx_parser_t parser = {.state = RX_WAIT_START};

    uint8_t frame[] = {PROTO_START_BYTE, MSG_HEARTBEAT, 0x00, 0xFF}; /* 0xFF is wrong */

    parse_result_t result = PARSE_INCOMPLETE;
    for (int i = 0; i < (int)sizeof(frame); i++) {
        result = rx_parse_byte(&parser, frame[i]);
    }

    TEST_ASSERT_EQUAL(PARSE_ERROR, result);
    TEST_ASSERT_EQUAL(RX_WAIT_START, parser.state);  /* must resync */
}

void test_parser_rejects_oversized_payload_length(void)
{
    /* A payload_len field larger than PROTO_MAX_PAYLOAD_LEN is a protocol
     * violation — the parser must resync immediately without reading payload. */
    rx_parser_t parser = {.state = RX_WAIT_START};

    uint8_t frame[] = {PROTO_START_BYTE, MSG_SENSOR_DATA, PROTO_MAX_PAYLOAD_LEN + 1};

    parse_result_t result = PARSE_INCOMPLETE;
    for (int i = 0; i < (int)sizeof(frame); i++) {
        result = rx_parse_byte(&parser, frame[i]);
    }

    TEST_ASSERT_EQUAL(PARSE_ERROR, result);
    TEST_ASSERT_EQUAL(RX_WAIT_START, parser.state);
}

void test_parser_resyncs_after_garbage_bytes(void)
{
    /* Garbage bytes before the start byte must be silently ignored.
     * After the garbage, a valid frame must still be accepted. */
    rx_parser_t parser = {.state = RX_WAIT_START};

    uint8_t garbage[] = {0x11, 0x22, 0x33};
    for (int i = 0; i < (int)sizeof(garbage); i++) {
        rx_parse_byte(&parser, garbage[i]);
    }
    TEST_ASSERT_EQUAL(RX_WAIT_START, parser.state);

    /* Now feed a valid heartbeat */
    uint8_t checksum = calc_checksum(MSG_HEARTBEAT, 0, NULL);
    uint8_t frame[]  = {PROTO_START_BYTE, MSG_HEARTBEAT, 0x00, checksum};

    parse_result_t result = PARSE_INCOMPLETE;
    for (int i = 0; i < (int)sizeof(frame); i++) {
        result = rx_parse_byte(&parser, frame[i]);
    }
    TEST_ASSERT_EQUAL(PARSE_COMPLETE, result);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_checksum_is_deterministic);
    RUN_TEST(test_checksum_changes_with_different_input);

    RUN_TEST(test_parser_accepts_valid_heartbeat_frame);
    RUN_TEST(test_parser_rejects_wrong_checksum);
    RUN_TEST(test_parser_rejects_oversized_payload_length);
    RUN_TEST(test_parser_resyncs_after_garbage_bytes);

    return UNITY_END();
}
