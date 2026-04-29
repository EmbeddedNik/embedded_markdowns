/*
 * @file    monitor_task.h
 * @brief   Monitor task for esp_logic: sensor staleness checks and fault detection
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef MONITOR_TASK_H
#define MONITOR_TASK_H

/* ── Public API ──────────────────────────────────────────────────── */
void monitor_task_init(void);
void monitor_task(void *arg);

#endif /* MONITOR_TASK_H */
