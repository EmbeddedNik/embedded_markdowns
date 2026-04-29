/*
 * @file    task_config.h
 * @brief   FreeRTOS task configuration: stack sizes, priorities, periods
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

/* ── sensor_task ─────────────────────────────────────────────────── */
#define SENSOR_TASK_STACK_SIZE      4096
#define SENSOR_TASK_PRIORITY        3
#define SENSOR_TASK_PERIOD_MS       10

/* ── actuator_task ───────────────────────────────────────────────── */
#define ACTUATOR_TASK_STACK_SIZE    4096
#define ACTUATOR_TASK_PRIORITY      3
#define ACTUATOR_TASK_PERIOD_MS     10

/* ── comm_task ───────────────────────────────────────────────────── */
#define COMM_TASK_STACK_SIZE        4096
#define COMM_TASK_PRIORITY          4
#define COMM_TASK_PERIOD_MS         10   /* safe at the default 100 Hz FreeRTOS tick rate */

/* ── watchdog_task ───────────────────────────────────────────────── */
#define WATCHDOG_TASK_STACK_SIZE    4096
#define WATCHDOG_TASK_PRIORITY      5
#define WATCHDOG_TASK_PERIOD_MS     1000

/* ── Shared resources ────────────────────────────────────────────── */
#define ACTUATOR_CMD_QUEUE_SIZE     10

/* ── Watchdog timeout ────────────────────────────────────────────── */
#define TWDT_TIMEOUT_MS             5000

/* ── Debug log interval ──────────────────────────────────────────── */
#define SENSOR_LOG_INTERVAL_MS      500

/* ── DHT11 read interval (min. 1000 ms per datasheet; 2000 ms avoids jitter failures) ── */
#define DHT_READ_INTERVAL_MS        2000
/* ── DHT11 fault suppression: consecutive failures before error flag is raised ── */
#define DHT_FAIL_THRESHOLD          3

/* ── Pump safety (last-resort hardware failsafe only) ────────────── */
#define PUMP_MAX_ON_MS              300000 /* 5 min absolute ceiling – only catches runaway */
#define PUMP_MIN_OFF_MS             1000   /* 1 s cool-down after ceiling triggered */

/* ── HWM log interval (in watchdog_task periods) ─────────────────── */
#define HWM_LOG_INTERVAL_CYCLES     10

/* ── Minimum acceptable stack headroom (words) ──────────────────── */
#define TASK_MIN_STACK_HWM          512

#endif /* TASK_CONFIG_H */
