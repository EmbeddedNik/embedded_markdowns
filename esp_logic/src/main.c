/*
 * @file    main.c
 * @brief   Entry point for esp_logic (Master ESP32 – Logic & Control).
 *          Initialises all subsystems and creates the five FreeRTOS tasks.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "task_config.h"
#include "comm_task.h"
#include "control_task.h"
#include "display_task.h"
#include "monitor_task.h"
#include "serial_task.h"
#include "watchdog_task.h"
#include "wifi_task.h"

static const char *TAG = "main";

/* ── Global task handles (extern'd in watchdog_task.h) ───────────── */
TaskHandle_t g_control_task_handle  = NULL;
TaskHandle_t g_comm_task_handle     = NULL;
TaskHandle_t g_display_task_handle  = NULL;
TaskHandle_t g_monitor_task_handle  = NULL;
TaskHandle_t g_serial_task_handle   = NULL;
TaskHandle_t g_watchdog_task_handle = NULL;
TaskHandle_t g_wifi_task_handle     = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "esp_logic starting ");

    /* Init order: watchdog first (configures TWDT), then all subsystems */
    watchdog_task_init();
    comm_task_init();
    control_task_init();
    display_task_init();
    monitor_task_init();
    serial_task_init();
    wifi_task_init();

    xTaskCreate(control_task,  "control_task",  CONTROL_TASK_STACK_SIZE,
                NULL, CONTROL_TASK_PRIORITY,  &g_control_task_handle);

    xTaskCreate(comm_task,     "comm_task",     COMM_TASK_STACK_SIZE,
                NULL, COMM_TASK_PRIORITY,     &g_comm_task_handle);

    xTaskCreate(display_task,  "display_task",  DISPLAY_TASK_STACK_SIZE,
                NULL, DISPLAY_TASK_PRIORITY,  &g_display_task_handle);

    xTaskCreate(monitor_task,  "monitor_task",  MONITOR_TASK_STACK_SIZE,
                NULL, MONITOR_TASK_PRIORITY,  &g_monitor_task_handle);

    xTaskCreate(serial_task,   "serial_task",   SERIAL_TASK_STACK_SIZE,
                NULL, SERIAL_TASK_PRIORITY,   &g_serial_task_handle);

    xTaskCreate(watchdog_task, "watchdog_task", WATCHDOG_TASK_STACK_SIZE,
                NULL, WATCHDOG_TASK_PRIORITY, &g_watchdog_task_handle);

    xTaskCreate(wifi_task,    "wifi_task",    WIFI_TASK_STACK_SIZE,
                NULL, WIFI_TASK_PRIORITY,    &g_wifi_task_handle);

    configASSERT(g_control_task_handle  != NULL);
    configASSERT(g_comm_task_handle     != NULL);
    configASSERT(g_display_task_handle  != NULL);
    configASSERT(g_monitor_task_handle  != NULL);
    configASSERT(g_serial_task_handle   != NULL);
    configASSERT(g_watchdog_task_handle != NULL);
    configASSERT(g_wifi_task_handle     != NULL);

}
