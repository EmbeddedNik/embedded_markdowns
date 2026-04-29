/*
 * @file    monitor_task.c
 * @brief   Monitor task for esp_logic: sensor staleness and fault detection.
 *          Checks data freshness and error_flags from esp_hardware.
 *          UC3 state ownership stays in control_task.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "monitor_task.h"
#include "comm_task.h"
#include "display_task.h"
#include "task_config.h"
#include "protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "monitor_task";

/* ── Public init ─────────────────────────────────────────────────── */
void monitor_task_init(void)
{
}

/* ── Task loop ───────────────────────────────────────────────────── */
void monitor_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t      last_wake      = xTaskGetTickCount();
    bool            stale_warned   = false;
    uint8_t         last_err_flags = 0;

    for (;;) {
        esp_task_wdt_reset();

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
        uint32_t age_ms = now_ms - g_rx_sensor_timestamp_ms;

        /* ── Staleness check ─────────────────────────────────────── */
        if (age_ms > SENSOR_STALE_MS) {
            if (!stale_warned) {
                ESP_LOGW(TAG, "Sensor data stale: last update %ums ago", (unsigned)age_ms);
                stale_warned = true;
            }
            /* Communication fault: cannot determine system state safely */
            goto next_cycle;
        }
        stale_warned = false;

        /* ── Snapshot sensor data ─────────────────────────────────── */
        sensor_data_t snap;
        if (xSemaphoreTake(g_rx_sensor_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
            ESP_LOGW(TAG, "Sensor mutex timeout");
            goto next_cycle;
        }
        snap = g_rx_sensor_data;
        xSemaphoreGive(g_rx_sensor_mutex);

        /* ── Fault flags from esp_hardware ──────────────────────── */
        if (snap.error_flags & ERROR_FLAG_UART_TIMEOUT) {
            ESP_LOGE(TAG, "esp_hardware in FAILSAFE (UART timeout)");
            goto next_cycle;
        }

        if (snap.error_flags != last_err_flags) {
            if (snap.error_flags) {
                ESP_LOGW(TAG, "Sensor fault(s): ldr=%d wlvl=%d steam=%d dist=%d temp=%d hum=%d",
                         !!(snap.error_flags & ERROR_FLAG_LDR),
                         !!(snap.error_flags & ERROR_FLAG_WATER_LEVEL),
                         !!(snap.error_flags & ERROR_FLAG_STEAM),
                         !!(snap.error_flags & ERROR_FLAG_DISTANCE),
                         !!(snap.error_flags & ERROR_FLAG_TEMPERATURE),
                         !!(snap.error_flags & ERROR_FLAG_HUMIDITY));
            } else {
                ESP_LOGI(TAG, "All sensors OK");
            }
            last_err_flags = snap.error_flags;
        }

        /* ── All checks passed: system normal ────────────────────── */
next_cycle:
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MONITOR_TASK_PERIOD_MS));
    }
}
