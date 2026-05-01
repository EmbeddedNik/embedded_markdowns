/*
 * @file    wifi_task.h
 * @brief   WiFi and HTTP server task for esp_logic: UC4.2–UC4.4 web control.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#ifndef WIFI_TASK_H
#define WIFI_TASK_H

void wifi_task_init(void);
void wifi_task(void *arg);

#endif /* WIFI_TASK_H */
