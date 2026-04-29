/*
 * @file    comm_task.c
 * @brief   UART2 communication task for esp_hardware (UC1.4).
 *          TX: serialises hw_sensor_data_t → protocol frame → UART2 every 100 ms.
 *          RX: parses incoming frames, dispatches MSG_ACTUATOR_CMD to actuator queue.
 *          FAILSAFE: no actuator command received in 500 ms → push safe state.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "comm_task.h"
#include "task_config.h"
#include "sensor_task.h"
#include "actuator_task.h"
#include "protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "comm_task";

/* ── UART RX buffer ──────────────────────────────────────────────── */
#define UART_RX_BUF_SIZE    512

/* ── Frame parser state machine ──────────────────────────────────── */
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

/* ── Helper: compute XOR checksum ───────────────────────────────── */
static uint8_t calc_checksum(uint8_t msg_type, uint8_t payload_len,
                              const uint8_t *payload)
{
    uint8_t cs = msg_type ^ payload_len;
    for (uint8_t i = 0; i < payload_len; i++) {
        cs ^= payload[i];
    }
    return cs;
}

/* ── Helper: build and send one protocol frame ───────────────────── */
static void send_frame(uint8_t msg_type, const uint8_t *payload,
                       uint8_t payload_len)
{
    /* buf = [START | MSG_TYPE | LEN | payload... | CHECKSUM] */
    uint8_t buf[3 + PROTO_MAX_PAYLOAD_LEN + 1];
    buf[0] = PROTO_START_BYTE;
    buf[1] = msg_type;
    buf[2] = payload_len;
    if (payload_len > 0) {
        memcpy(&buf[3], payload, payload_len);
    }
    buf[3 + payload_len] = calc_checksum(msg_type, payload_len, payload);
    uart_write_bytes(COMM_UART_NUM, (const char *)buf, 3 + payload_len + 1);
}

/* ── Build and transmit MSG_SENSOR_DATA from g_sensor_data ───────── */
static void tx_sensor_data(bool failsafe_active)
{
    hw_sensor_data_t snap;

    if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        snap = g_sensor_data;
        xSemaphoreGive(g_sensor_mutex);
    } else {
        ESP_LOGW(TAG, "Sensor mutex timeout – skipping TX");
        return;
    }

    /* Convert internal float representation to wire integer format */
    sensor_data_t wire;
    wire.water_level    = (uint16_t)(snap.water_level_raw   < 0 ? 0 : snap.water_level_raw);
    wire.soil_humidity  = 0u;   /* soil sensor not used in this project */
    wire.photoresistor  = (uint16_t)(snap.ldr_raw           < 0 ? 0 : snap.ldr_raw);
    wire.steam_sensor   = (uint16_t)(snap.steam_raw         < 0 ? 0 : snap.steam_raw);
    wire.temperature    = (int16_t)(snap.temperature_c  * 10.0f);
    wire.humidity       = (uint16_t)(snap.humidity_pct  * 10.0f);
    wire.ultrasonic_mm  = (uint16_t)(snap.distance_cm   * 10.0f);  /* cm → mm */
    wire.pir_detected   = snap.pir_detected   ? 1u : 0u;
    wire.button_pressed = snap.button_pressed ? 1u : 0u;

    wire.error_flags = 0;
    if (snap.err_ldr)         wire.error_flags |= ERROR_FLAG_LDR;
    if (snap.err_water_level) wire.error_flags |= ERROR_FLAG_WATER_LEVEL;
    if (snap.err_steam)       wire.error_flags |= ERROR_FLAG_STEAM;
    if (snap.err_distance)    wire.error_flags |= ERROR_FLAG_DISTANCE;
    if (snap.err_temperature) wire.error_flags |= ERROR_FLAG_TEMPERATURE;
    if (snap.err_humidity)    wire.error_flags |= ERROR_FLAG_HUMIDITY;
    if (failsafe_active)      wire.error_flags |= ERROR_FLAG_UART_TIMEOUT;

    send_frame(MSG_SENSOR_DATA, (const uint8_t *)&wire, (uint8_t)sizeof(wire));
}

/* ── Drain queue then send one item (safe replacement for xQueueOverwrite
 *    on queues with length > 1) ───────────────────────────────────── */
static void send_to_actuator_queue(const hw_actuator_cmd_t *cmd)
{
    hw_actuator_cmd_t discard;
    while (xQueueReceive(g_actuator_queue, &discard, 0) == pdTRUE) {}
    xQueueSend(g_actuator_queue, cmd, 0);
}

/* ── Push FAILSAFE (all actuators safe) to actuator queue ────────── */
static void push_failsafe(void)
{
    hw_actuator_cmd_t safe = {
        .fan_enable      = false,
        .fan_duty        = 0,
        .servo_enable    = false,
        .servo_angle_deg = 0,
        .pump_enable     = false,
        .buzzer_enable   = false,
        .led_enable      = false,
        .display_mode    = 0,
        .profile         = 1,
    };
    send_to_actuator_queue(&safe);
    ESP_LOGW(TAG, "FAILSAFE: no actuator cmd for %d ms – safe state applied",
             COMM_FAILSAFE_TIMEOUT_MS);
}

/* ── Translate protocol actuator_cmd_t → hw_actuator_cmd_t ─────── */
static hw_actuator_cmd_t proto_to_hw_cmd(const actuator_cmd_t *p)
{
    hw_actuator_cmd_t hw = {0};

    hw.pump_enable = (p->pump_on != 0);

    if (p->fan_speed != 0u) {
        hw.fan_enable = true;
        switch (p->profile) {
            case 0:  hw.fan_duty = FAN_DUTY_ECO;         break;
            case 2:  hw.fan_duty = FAN_DUTY_PERFORMANCE; break;
            case 1:
            default: hw.fan_duty = FAN_DUTY_NORMAL;      break;
        }
    } else {
        hw.fan_enable = false;
        hw.fan_duty = 0;
    }

    /* servo_position is a logical hatch percentage, mapped to calibrated travel. */
    uint8_t servo_position = p->servo_position;
    if (servo_position > 100u) {
        servo_position = 100u;
    }
    int32_t servo_span = (int32_t)SERVO_OPEN_ANGLE_DEG - (int32_t)SERVO_CLOSED_ANGLE_DEG;
    int32_t servo_angle = (int32_t)SERVO_CLOSED_ANGLE_DEG +
                          (servo_span * (int32_t)servo_position) / 100;
    if (servo_angle < 0) {
        servo_angle = 0;
    } else if (servo_angle > 180) {
        servo_angle = 180;
    }
    hw.servo_enable    = true;
    hw.servo_angle_deg = (uint16_t)servo_angle;

    hw.led_enable    = (p->led_on    != 0);
    hw.buzzer_enable = (p->buzzer_on != 0);
    hw.display_mode  = p->display_mode;
    hw.profile       = p->profile;

    return hw;
}

/* ── Feed one byte into the RX state machine ─────────────────────── */
static void rx_parse_byte(rx_parser_t *rx, uint8_t byte,
                           int64_t *last_cmd_us, bool *failsafe_active)
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
                /* Invalid length: discard and resync */
                ESP_LOGW(TAG, "Invalid payload_len %u – resyncing", byte);
                rx->state = RX_WAIT_START;
            } else {
                rx->payload_len = byte;
                rx->payload_idx = 0;
                rx->state       = (byte > 0) ? RX_PAYLOAD : RX_CHECKSUM;
            }
            break;

        case RX_PAYLOAD:
            rx->payload[rx->payload_idx++] = byte;
            if (rx->payload_idx >= rx->payload_len) {
                rx->state = RX_CHECKSUM;
            }
            break;

        case RX_CHECKSUM: {
            uint8_t expected = calc_checksum(rx->msg_type, rx->payload_len,
                                             rx->payload);
            if (byte != expected) {
                ESP_LOGW(TAG, "Checksum mismatch: got 0x%02X expected 0x%02X",
                         byte, expected);
                rx->state = RX_WAIT_START;
                break;
            }

            /* Valid frame received */
            if (rx->msg_type == MSG_ACTUATOR_CMD &&
                rx->payload_len == sizeof(actuator_cmd_t)) {

                actuator_cmd_t proto_cmd;
                memcpy(&proto_cmd, rx->payload, sizeof(proto_cmd));

                hw_actuator_cmd_t hw_cmd = proto_to_hw_cmd(&proto_cmd);
                send_to_actuator_queue(&hw_cmd);

                *last_cmd_us    = esp_timer_get_time();
                *failsafe_active = false;
            }

            rx->state = RX_WAIT_START;
            break;
        }
    }
}

/* ── Public init ─────────────────────────────────────────────────── */
void comm_task_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = COMM_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(COMM_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(COMM_UART_NUM, COMM_PIN_TX, COMM_PIN_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(COMM_UART_NUM, UART_RX_BUF_SIZE, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "UART2 ready (TX=io%d RX=io%d baud=%d)",
             COMM_PIN_TX, COMM_PIN_RX, COMM_BAUD_RATE);
}

/* ── Task loop ───────────────────────────────────────────────────── */
void comm_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t  last_wake   = xTaskGetTickCount();
    rx_parser_t rx          = {.state = RX_WAIT_START};
    int64_t     last_cmd_us = esp_timer_get_time();  /* initialise to now */
    bool        failsafe_active = false;

    /* Cycle counter for 100 ms TX interval (period = COMM_TASK_PERIOD_MS) */
    const uint32_t TX_EVERY = COMM_SENSOR_TX_MS / COMM_TASK_PERIOD_MS;
    uint32_t       tx_counter = 0;

    const int64_t FAILSAFE_US = (int64_t)COMM_FAILSAFE_TIMEOUT_MS * 1000LL;

    for (;;) {
        esp_task_wdt_reset();

        /* ── RX: drain UART buffer through state machine ─────────── */
        uint8_t byte;
        while (uart_read_bytes(COMM_UART_NUM, &byte, 1, 0) > 0) {
            rx_parse_byte(&rx, byte, &last_cmd_us, &failsafe_active);
        }

        /* ── FAILSAFE check ──────────────────────────────────────── */
        int64_t now_us = esp_timer_get_time();
        if (!failsafe_active && (now_us - last_cmd_us) > FAILSAFE_US) {
            failsafe_active = true;
            push_failsafe();
        }

        /* ── TX: send sensor data every 100 ms ───────────────────── */
        if (tx_counter == 0) {
            tx_sensor_data(failsafe_active);
        }
        tx_counter = (tx_counter + 1) % TX_EVERY;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMM_TASK_PERIOD_MS));
    }
}
