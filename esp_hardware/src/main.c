/*
 * @file    main.c
 * @brief   Entry point for esp_hardware (Slave ESP32 / Smart Farm Kit).
 *          Creates all FreeRTOS tasks and initialises the Task Watchdog Timer.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "task_config.h"
#include "sensor_task.h"
#include "actuator_task.h"
#include "comm_task.h"
#include "watchdog_task.h"

static const char *TAG = "main";

/* ── Global task handles (extern'd in watchdog_task.h) ───────────── */
TaskHandle_t g_sensor_task_handle   = NULL;
TaskHandle_t g_actuator_task_handle = NULL;
TaskHandle_t g_comm_task_handle     = NULL;
TaskHandle_t g_watchdog_task_handle = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "esp_hardware starting – UC1.2 FreeRTOS skeleton");

    /* Initialise shared resources and peripherals before task creation */
    watchdog_task_init();   /* configure TWDT first */
    sensor_task_init();
    actuator_task_init();
    comm_task_init();

    /* Create tasks; handles are stored so watchdog_task can subscribe them */
    xTaskCreate(sensor_task,   "sensor_task",   SENSOR_TASK_STACK_SIZE,
                NULL, SENSOR_TASK_PRIORITY,   &g_sensor_task_handle);

    xTaskCreate(actuator_task, "actuator_task", ACTUATOR_TASK_STACK_SIZE,
                NULL, ACTUATOR_TASK_PRIORITY, &g_actuator_task_handle);

    xTaskCreate(comm_task,     "comm_task",     COMM_TASK_STACK_SIZE,
                NULL, COMM_TASK_PRIORITY,     &g_comm_task_handle);

    xTaskCreate(watchdog_task, "watchdog_task", WATCHDOG_TASK_STACK_SIZE,
                NULL, WATCHDOG_TASK_PRIORITY, &g_watchdog_task_handle);

    configASSERT(g_sensor_task_handle   != NULL);
    configASSERT(g_actuator_task_handle != NULL);
    configASSERT(g_comm_task_handle     != NULL);
    configASSERT(g_watchdog_task_handle != NULL);

    ESP_LOGI(TAG, "All tasks created – scheduler running");
    /* app_main returns; the FreeRTOS scheduler continues */
}
