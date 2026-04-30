/*
 * @file    display_task.c
 * @brief   Display task for esp_logic.
 *          Maps g_system_state → actuator_cmd_t.display_mode sent to esp_hardware.
 *          esp_hardware's actuator_task drives the physical LCD based on this value.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "display_task.h"
#include "comm_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "display_task";

/* Shared system state written by control_task and read by display_task. */
volatile system_state_t g_system_state = SYS_STATE_OK;

/* ── Public init ─────────────────────────────────────────────────── */
void display_task_init(void)
{
    g_system_state = SYS_STATE_OK;
}

/* ── Task loop ───────────────────────────────────────────────────── */
void display_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t        last_wake    = xTaskGetTickCount();
    system_state_t    last_state   = SYS_STATE_OK;

    for (;;) {
        esp_task_wdt_reset();

        system_state_t current_state = g_system_state;

        /* Only update display_mode when state actually changes */
        if (current_state != last_state) {
            ESP_LOGI(TAG, "System state → %d", (int)current_state);
            last_state = current_state;
        }

        /* Write display_mode into the outgoing actuator command */
        if (xSemaphoreTake(g_tx_cmd_mutex, portMAX_DELAY) == pdTRUE) {
            g_tx_actuator_cmd.display_mode = (uint8_t)current_state;
            xSemaphoreGive(g_tx_cmd_mutex);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DISPLAY_TASK_PERIOD_MS));
    }
}
