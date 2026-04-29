/*
 * @file    watchdog_task.h
 * @brief   Watchdog task for esp_logic: TWDT management and stack monitoring
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ── Global task handles (defined in main.c) ─────────────────────── */
extern TaskHandle_t g_control_task_handle;
extern TaskHandle_t g_comm_task_handle;
extern TaskHandle_t g_display_task_handle;
extern TaskHandle_t g_monitor_task_handle;
extern TaskHandle_t g_serial_task_handle;
extern TaskHandle_t g_watchdog_task_handle;

/* ── Public API ──────────────────────────────────────────────────── */
void watchdog_task_init(void);
void watchdog_task(void *arg);

#endif /* WATCHDOG_TASK_H */
