/*
 * @file    control_task.h
 * @brief   Control task for esp_logic: UC3 profile-based load steering.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

typedef enum {
    SYSTEM_PROFILE_ECO = 0,
    SYSTEM_PROFILE_NORMAL = 1,
    SYSTEM_PROFILE_PERFORMANCE = 2,
} system_profile_t;

extern volatile system_profile_t g_active_profile;

void control_task_init(void);
void control_task(void *arg);

#endif /* CONTROL_TASK_H */
