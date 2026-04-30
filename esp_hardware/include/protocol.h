/*
 * @file    protocol.h
 * @brief   Shared UART2 protocol definitions for esp_hardware <-> esp_logic communication
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Frame constants
 * ---------------------------------------------------------------------- */

#define PROTO_START_BYTE      ((uint8_t)0xAA)
#define PROTO_MAX_PAYLOAD_LEN ((uint8_t)32)

/* -------------------------------------------------------------------------
 * Message type identifiers
 * ---------------------------------------------------------------------- */

#define MSG_SENSOR_DATA       ((uint8_t)0x01)  /* esp_hardware -> esp_logic */
#define MSG_ACTUATOR_CMD      ((uint8_t)0x02)  /* esp_logic -> esp_hardware */
#define MSG_HEARTBEAT         ((uint8_t)0x03)  /* esp_hardware -> esp_logic, empty payload */
#define MSG_ACK               ((uint8_t)0x04)  /* both directions,            empty payload */

/* -------------------------------------------------------------------------
 * error_flags bitmask (sensor_data_t.error_flags)
 * ---------------------------------------------------------------------- */

#define ERROR_FLAG_LDR            ((uint8_t)0x01)  /* bit 0: photoresistor ADC failure */
#define ERROR_FLAG_WATER_LEVEL    ((uint8_t)0x02)  /* bit 1: water level ADC failure */
#define ERROR_FLAG_STEAM          ((uint8_t)0x04)  /* bit 2: steam sensor ADC failure */
#define ERROR_FLAG_DISTANCE       ((uint8_t)0x08)  /* bit 3: ultrasonic timeout */
#define ERROR_FLAG_TEMPERATURE    ((uint8_t)0x10)  /* bit 4: DHT temperature failure */
#define ERROR_FLAG_HUMIDITY       ((uint8_t)0x20)  /* bit 5: DHT humidity failure */
/* bit 6: reserved */
#define ERROR_FLAG_UART_TIMEOUT   ((uint8_t)0x80)  /* bit 7: UART communication timeout */

/* -------------------------------------------------------------------------
 * Sensor data payload – MSG_SENSOR_DATA (17 bytes, packed)
 *
 * All floating-point values are scaled to integers:
 *   temperature: degrees C * 10  (e.g. 235 = 23.5 °C)
 *   humidity:    percent  * 10   (e.g. 654 = 65.4 %)
 * ---------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint16_t water_level;      /* raw ADC 0-4095 (io33) */
    uint16_t soil_humidity;    /* raw ADC 0-4095 (io32) */
    uint16_t photoresistor;    /* raw ADC 0-4095 (io34) */
    uint16_t steam_sensor;     /* raw ADC 0-4095 (io35) */
    int16_t  temperature;      /* degrees C * 10, range -400..800 (io17) */
    uint16_t humidity;         /* percent * 10, range 0..1000 (io17) */
    uint16_t ultrasonic_mm;    /* distance in mm, range 0..4000 (io12/io13) */
    uint8_t  pir_detected;     /* 0 = no motion, 1 = motion detected (io23) */
    uint8_t  button_pressed;   /* 0 = released, 1 = pressed (io5) */
    uint8_t  error_flags;      /* bitmask: ERROR_FLAG_* constants */
} sensor_data_t;

/* -------------------------------------------------------------------------
 * Actuator command payload - MSG_ACTUATOR_CMD (7 bytes)
 * ---------------------------------------------------------------------- */

typedef struct {
    uint8_t  pump_on;          /* 0 = off, 1 = on (io25) */
    uint8_t  fan_speed;        /* 0 = off, nonzero = on (io19 PWM, io18 LOW) */
    uint8_t  servo_position;   /* 0 = closed, 100 = open (io26) */
    uint8_t  led_on;           /* 0 = off, 1 = on (io27) */
    uint8_t  buzzer_on;        /* 0 = off, 1 = on (io16) */
    uint8_t  display_mode;     /* 0 = OK, 1 = REFILL */
    uint8_t  profile;          /* 0 = eco, 1 = normal, 2 = performance */
} actuator_cmd_t;

/* -------------------------------------------------------------------------
 * Full UART frame – wrapper for all message types (36 bytes)
 * ---------------------------------------------------------------------- */

typedef struct {
    uint8_t start_byte;                        /* always PROTO_START_BYTE (0xAA) */
    uint8_t msg_type;                          /* MSG_* constant */
    uint8_t payload_len;                       /* number of valid bytes in payload */
    uint8_t payload[PROTO_MAX_PAYLOAD_LEN];    /* message payload */
    uint8_t checksum;                          /* XOR of msg_type, payload_len, all payload bytes */
} uart_frame_t;

/* -------------------------------------------------------------------------
 * Compile-time size verification
 *
 * sensor_data_t : 7 * uint16_t (14) + 3 * uint8_t (3) = 17 bytes (packed)
 * actuator_cmd_t: 7 * uint8_t  (7)                     =  7 bytes
 * uart_frame_t  : 3 * uint8_t  (3) + uint8_t[32] (32)
 *                 + 1 * uint8_t (1)                    = 36 bytes
 * ---------------------------------------------------------------------- */

_Static_assert(sizeof(sensor_data_t)  == 17, "sensor_data_t must be 17 bytes (packed)");
_Static_assert(sizeof(actuator_cmd_t) ==  7, "actuator_cmd_t must be 7 bytes");
_Static_assert(sizeof(uart_frame_t)   == 36, "uart_frame_t must be 36 bytes");

/* Verify payload structs fit within the maximum payload size */
_Static_assert(sizeof(sensor_data_t)  <= PROTO_MAX_PAYLOAD_LEN,
               "sensor_data_t exceeds PROTO_MAX_PAYLOAD_LEN");
_Static_assert(sizeof(actuator_cmd_t) <= PROTO_MAX_PAYLOAD_LEN,
               "actuator_cmd_t exceeds PROTO_MAX_PAYLOAD_LEN");

#endif /* PROTOCOL_H */
