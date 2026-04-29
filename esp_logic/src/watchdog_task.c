/*
 * @file    watchdog_task.c
 * @brief   Task Watchdog Timer management for esp_logic.
 *          Subscribes all tasks and logs stack high-water marks every 10 s.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "watchdog_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_task_wdt.h"
#include "esp_log.h"

static const char *TAG = "watchdog_task";

/* ── Public init (call before scheduler starts) ───────────────────── */
void watchdog_task_init(void)
{
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = TWDT_TIMEOUT_MS,
        .idle_core_mask = 0,          /* do not watch idle tasks */
        .trigger_panic  = true,
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
}

/* ── Task loop ───────────────────────────────────────────────────── */
void watchdog_task(void *arg)
{
    esp_task_wdt_add(NULL);

    /* Each task subscribes itself via esp_task_wdt_add(NULL). No explicit
     * handle subscriptions here to avoid double-subscription errors. */
    uint32_t cycle = 0;

    for (;;) {
        esp_task_wdt_reset();

        /* Log stack high-water marks every HWM_LOG_INTERVAL_CYCLES seconds */
        if (cycle > 0 && (cycle % HWM_LOG_INTERVAL_CYCLES) == 0) {
            UBaseType_t hwm;

            hwm = uxTaskGetStackHighWaterMark(g_control_task_handle);
            if (hwm < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: control_task");
            hwm = uxTaskGetStackHighWaterMark(g_comm_task_handle);
            if (hwm < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: comm_task");
            hwm = uxTaskGetStackHighWaterMark(g_display_task_handle);
            if (hwm < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: display_task");
            hwm = uxTaskGetStackHighWaterMark(g_monitor_task_handle);
            if (hwm < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: monitor_task");
            hwm = uxTaskGetStackHighWaterMark(g_serial_task_handle);
            if (hwm < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: serial_task");
            hwm = uxTaskGetStackHighWaterMark(NULL);
            if (hwm < TASK_MIN_STACK_HWM) ESP_LOGW(TAG, "LOW stack: watchdog_task");
        }
        cycle++;

        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_TASK_PERIOD_MS));
    }
}
