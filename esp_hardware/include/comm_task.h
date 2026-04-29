/*
 * @file    comm_task.h
 * @brief   UART2 communication task: public API and configuration
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef COMM_TASK_H
#define COMM_TASK_H

#include "driver/uart.h"

/* ── UART2 pin definitions (hardware-fixed, do not change) ───────── */
#define COMM_UART_NUM           UART_NUM_2
#define COMM_PIN_TX             2       /* esp_hardware → esp_logic */
#define COMM_PIN_RX             4       /* esp_logic   → esp_hardware */
#define COMM_BAUD_RATE          115200

/* ── Protocol timing ─────────────────────────────────────────────── */
#define COMM_SENSOR_TX_MS       100     /* send sensor data every 100 ms */
#define COMM_FAILSAFE_TIMEOUT_MS 500    /* no actuator cmd → FAILSAFE */

/* ── Public API ──────────────────────────────────────────────────── */
void comm_task_init(void);
void comm_task(void *arg);

#endif /* COMM_TASK_H */
