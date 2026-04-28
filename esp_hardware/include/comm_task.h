/*
 * @file    comm_task.h
 * @brief   Communication task (UART2 stub): public API
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#ifndef COMM_TASK_H
#define COMM_TASK_H

/* ── UART2 pin definitions (hardware-fixed, do not change) ───────── */
#define COMM_UART_NUM   UART_NUM_2
#define COMM_PIN_TX     2    /* esp_hardware → esp_logic */
#define COMM_PIN_RX     4    /* esp_logic   → esp_hardware */
#define COMM_BAUD_RATE  115200

/* ── Public API ──────────────────────────────────────────────────── */
void comm_task_init(void);
void comm_task(void *arg);

#endif /* COMM_TASK_H */
