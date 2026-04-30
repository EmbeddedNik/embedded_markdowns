/*
 * @file    display_task.h
 * @brief   Display task for esp_logic: determines display_mode sent to esp_hardware
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include <stdint.h>

/*
 * System state shared between monitor_task (writer) and display_task (reader).
 * Higher numeric value = higher severity; display_task maps this 1:1 to
 * actuator_cmd_t.display_mode sent to esp_hardware.
 */
typedef enum {
    SYS_STATE_OK     = 0,
    SYS_STATE_REFILL = 1,
} system_state_t;

/* Written by control_task; read by display_task */
extern volatile system_state_t g_system_state;

/* ── Public API ──────────────────────────────────────────────────── */
void display_task_init(void);
void display_task(void *arg);

#endif /* DISPLAY_TASK_H */
