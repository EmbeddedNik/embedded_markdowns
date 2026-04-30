/*
 * @file    comm_task.c
 * @brief   UART2 communication task for esp_logic (UC1.3 + UC1.4).
 *          RX: parses incoming frames from esp_hardware, updates g_rx_sensor_data.
 *          TX: reads g_tx_actuator_cmd and sends MSG_ACTUATOR_CMD every cycle.
 *          Heartbeat timeout: no sensor frame for COMM_HB_TIMEOUT_MS → log warning.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "comm_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "comm_task";

/* ── UART RX buffer ──────────────────────────────────────────────── */
#define UART_RX_BUF_SIZE    512
#define COMM_LINK_WARN_REPEAT_MS 5000

/* ── Shared globals (declared extern in comm_task.h) ─────────────── */
sensor_data_t     g_rx_sensor_data;
SemaphoreHandle_t g_rx_sensor_mutex;
volatile uint32_t g_rx_sensor_timestamp_ms = 0;
volatile uint8_t  g_rx_data_valid = 0;

actuator_cmd_t    g_tx_actuator_cmd;
SemaphoreHandle_t g_tx_cmd_mutex;

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

/* ── CRC-8 (polynomial 0x07, initial value 0x00) ────────────────── */
static uint8_t crc8_byte(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
    }
    return crc;
}

static uint8_t calc_checksum(uint8_t msg_type, uint8_t payload_len,
                              const uint8_t *payload)
{
    uint8_t crc = crc8_byte(0x00u, msg_type);
    crc = crc8_byte(crc, payload_len);
    for (uint8_t i = 0; i < payload_len; i++) {
        crc = crc8_byte(crc, payload[i]);
    }
    return crc;
}

/* ── Helper: build and send one protocol frame ───────────────────── */
static void send_frame(uint8_t msg_type, const uint8_t *payload,
                       uint8_t payload_len)
{
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

/* ── Process a complete valid frame ──────────────────────────────── */
static void handle_frame(uint8_t msg_type, uint8_t payload_len,
                          const uint8_t *payload)
{
    if (msg_type == MSG_SENSOR_DATA && payload_len == sizeof(sensor_data_t)) {
        sensor_data_t received;
        memcpy(&received, payload, sizeof(received));

        if (xSemaphoreTake(g_rx_sensor_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_rx_sensor_data = received;
            xSemaphoreGive(g_rx_sensor_mutex);
        } else {
            ESP_LOGW(TAG, "RX mutex timeout – sensor data discarded");
            return;
        }

        /* Update timestamp after successful write */
        g_rx_sensor_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
        g_rx_data_valid = 1u;

    } else if (msg_type == MSG_HEARTBEAT && payload_len == 0) {
        /* Heartbeat: update timestamp but no payload to store */
        g_rx_sensor_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    }
    /* MSG_ACK and unknown types are silently ignored */
}

/* ── Feed one byte into the RX state machine ─────────────────────── */
static void rx_parse_byte(rx_parser_t *rx, uint8_t byte)
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
            } else {
                handle_frame(rx->msg_type, rx->payload_len, rx->payload);
            }
            rx->state = RX_WAIT_START;
            break;
        }
    }
}

/* ── Send latest g_tx_actuator_cmd ───────────────────────────────── */
static void tx_actuator_cmd(void)
{
    actuator_cmd_t snap;
    if (xSemaphoreTake(g_tx_cmd_mutex, portMAX_DELAY) == pdTRUE) {
        snap = g_tx_actuator_cmd;
        xSemaphoreGive(g_tx_cmd_mutex);
    }
    send_frame(MSG_ACTUATOR_CMD, (const uint8_t *)&snap, (uint8_t)sizeof(snap));
}

/* ── Public init ─────────────────────────────────────────────────── */
void comm_task_init(void)
{
    memset(&g_rx_sensor_data, 0, sizeof(g_rx_sensor_data));
    g_rx_data_valid = 0u;
    g_rx_sensor_mutex = xSemaphoreCreateMutex();
    configASSERT(g_rx_sensor_mutex != NULL);

    memset(&g_tx_actuator_cmd, 0, sizeof(g_tx_actuator_cmd));
    g_tx_cmd_mutex = xSemaphoreCreateMutex();
    configASSERT(g_tx_cmd_mutex != NULL);

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

    /* Initialise timestamp so monitor_task doesn't immediately warn */
    g_rx_sensor_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL);

    ESP_LOGI(TAG, "UART2 ready (TX=io%d RX=io%d baud=%d)",
             COMM_PIN_TX, COMM_PIN_RX, COMM_BAUD_RATE);
}

/* ── Task loop ───────────────────────────────────────────────────── */
void comm_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t  last_wake   = xTaskGetTickCount();
    rx_parser_t rx          = {.state = RX_WAIT_START};
    bool        hb_warned   = false;
    uint32_t    last_hb_warn_ms = 0;

    const int64_t HB_TIMEOUT_MS = COMM_HB_TIMEOUT_MS;

    for (;;) {
        esp_task_wdt_reset();

        /* ── RX: drain UART buffer through state machine ─────────── */
        uint8_t byte;
        while (uart_read_bytes(COMM_UART_NUM, &byte, 1, 0) > 0) {
            rx_parse_byte(&rx, byte);
        }

        /* ── Heartbeat timeout monitoring ────────────────────────── */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
        uint32_t age_ms = now_ms - g_rx_sensor_timestamp_ms;

        if (age_ms > HB_TIMEOUT_MS) {
            if (!hb_warned ||
                (uint32_t)(now_ms - last_hb_warn_ms) >= COMM_LINK_WARN_REPEAT_MS) {
                ESP_LOGE(TAG, "esp_hardware link lost: no sensor data for %ums",
                         (unsigned)age_ms);
                hb_warned = true;
                last_hb_warn_ms = now_ms;
            }
        } else {
            if (hb_warned) {
                ESP_LOGI(TAG, "esp_hardware link restored");
            }
            hb_warned = false;
        }

        /* ── TX: send actuator command every cycle (10 ms) ─────────── */
        tx_actuator_cmd();

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMM_TASK_PERIOD_MS));
    }
}
