/*
 * @file    comm_task.h
 * @brief   UART2 communication task for esp_logic: public API and shared data
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef COMM_TASK_H
#define COMM_TASK_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "protocol.h"

/* ── UART2 pin definitions (hardware-fixed, do not change) ───────── */
#define COMM_UART_NUM       UART_NUM_2
#define COMM_PIN_TX         33     /* esp_logic → esp_hardware */
#define COMM_PIN_RX         32     /* esp_hardware → esp_logic */
#define COMM_BAUD_RATE      115200

/* ── Heartbeat timeout (ms) ──────────────────────────────────────── */
#define COMM_HB_TIMEOUT_MS  500

/* ── Last received sensor data (updated by comm_task on each valid frame) */
extern sensor_data_t     g_rx_sensor_data;
extern SemaphoreHandle_t g_rx_sensor_mutex;
extern volatile uint8_t  g_rx_data_valid;

/* Timestamp (ms via esp_timer) of the last successfully received sensor frame */
extern volatile uint32_t g_rx_sensor_timestamp_ms;

/* ── Outgoing actuator command (written by control/display/monitor tasks) */
extern actuator_cmd_t    g_tx_actuator_cmd;
extern SemaphoreHandle_t g_tx_cmd_mutex;

/* ── Public API ──────────────────────────────────────────────────── */
void comm_task_init(void);
void comm_task(void *arg);

#endif /* COMM_TASK_H */
