/*
 * @file    wifi_task.h
 * @brief   WiFi Access Point + HTTP server task for esp_logic (UC4.1).
 *          Starts the ESP32 in AP mode and registers HTTP URI handlers.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#ifndef WIFI_TASK_H
#define WIFI_TASK_H

/* ── Task configuration ──────────────────────────────────────────── */
#define WIFI_TASK_STACK_SIZE    8192
#define WIFI_TASK_PRIORITY      2

/* ── Public API ──────────────────────────────────────────────────── */
void wifi_task_init(void);
void wifi_task(void *arg);

#endif /* WIFI_TASK_H */
