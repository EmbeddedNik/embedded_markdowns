/*
 * @file    watchdog_task.c
 * @brief   Initialises the ESP Task Watchdog Timer, subscribes all tasks,
 *          and logs stack high-water marks every 10 s.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#include "watchdog_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task_wdt.h"
#include "esp_log.h"

static const char *TAG = "watchdog_task";

/* ── TWDT initialisation (called from main before task creation) ── */
void watchdog_task_init(void)
{
    /* Reconfigure the TWDT: 5 s timeout, trigger panic on expiry */
    const esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = TWDT_TIMEOUT_MS,
        .idle_core_mask = 0,    /* do not watch idle tasks */
        .trigger_panic  = true,
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
    ESP_LOGI(TAG, "TWDT configured: timeout=%u ms, panic=true", TWDT_TIMEOUT_MS);
}

/* ── Task loop ───────────────────────────────────────────────────── */
void watchdog_task(void *arg)
{
    /* Subscribe this task */
    esp_task_wdt_add(NULL);

    /* Wait until all other task handles are available, then subscribe them.
     * The handles are written by main() before the scheduler starts, so they
     * are already set when this task first runs after vTaskStartScheduler(). */
    if (g_sensor_task_handle   != NULL) esp_task_wdt_add(g_sensor_task_handle);
    if (g_actuator_task_handle != NULL) esp_task_wdt_add(g_actuator_task_handle);
    if (g_comm_task_handle     != NULL) esp_task_wdt_add(g_comm_task_handle);

    ESP_LOGI(TAG, "Subscribed sensor, actuator and comm tasks to TWDT");

    TickType_t last_wake    = xTaskGetTickCount();
    uint32_t   hwm_counter  = 0;

    for (;;) {
        esp_task_wdt_reset();

        /* Log stack high-water marks every HWM_LOG_INTERVAL_CYCLES seconds */
        if (hwm_counter == 0) {
            UBaseType_t hwm_sensor   = uxTaskGetStackHighWaterMark(g_sensor_task_handle);
            UBaseType_t hwm_actuator = uxTaskGetStackHighWaterMark(g_actuator_task_handle);
            UBaseType_t hwm_comm     = uxTaskGetStackHighWaterMark(g_comm_task_handle);
            UBaseType_t hwm_wdog     = uxTaskGetStackHighWaterMark(NULL);

            ESP_LOGI(TAG,
                     "Stack HWM (words) – sensor:%u actuator:%u comm:%u watchdog:%u",
                     hwm_sensor, hwm_actuator, hwm_comm, hwm_wdog);

            if (hwm_sensor   < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: sensor_task");
            if (hwm_actuator < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: actuator_task");
            if (hwm_comm     < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: comm_task");
            if (hwm_wdog     < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: watchdog_task");
        }
        hwm_counter = (hwm_counter + 1) % HWM_LOG_INTERVAL_CYCLES;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(WATCHDOG_TASK_PERIOD_MS));
    }
}
