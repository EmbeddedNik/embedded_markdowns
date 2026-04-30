/*
 * @file    control_task.c
 * @brief   UC3 profile-based load steering for esp_logic.
 *          No PI control: pump is a direct/manual load, water state only
 *          notifies via display and buzzer.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "control_task.h"
#include "control_logic.h"
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

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "control_task";

volatile system_profile_t g_active_profile = SYSTEM_PROFILE_NORMAL;

void control_task_init(void)
{
    g_active_profile = SYSTEM_PROFILE_NORMAL;
    ESP_LOGI(TAG, "UC3 profile load steering initialised (profile=NORMAL)");
}

void control_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t last_wake = xTaskGetTickCount();
    water_state_t water_state = WATER_STATE_OK;
    water_state_t last_water_state = WATER_STATE_OK;
    light_state_t light_state = LIGHT_STATE_DAY;
    uint32_t light_confirm_ticks = 0;
    system_profile_t last_profile = SYSTEM_PROFILE_NORMAL;
    uint32_t last_log_ms   = 0;
    uint32_t alarm_hold_ms  = 0;
    uint32_t night_guard_ms = 0;

    for (;;) {
        esp_task_wdt_reset();

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
        bool data_fresh = false;
        sensor_data_t snap = {0};

        if (g_rx_data_valid &&
            (uint32_t)(now_ms - g_rx_sensor_timestamp_ms) <= SENSOR_STALE_MS &&
            xSemaphoreTake(g_rx_sensor_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            snap = g_rx_sensor_data;
            xSemaphoreGive(g_rx_sensor_mutex);
            data_fresh = true;
        }

        system_profile_t profile = g_active_profile;
        actuator_cmd_t cmd = {0};
        cmd.profile = (uint8_t)profile;

        if (data_fresh) {
            bool ldr_ok   = !(snap.error_flags & ERROR_FLAG_LDR);
            bool temp_ok  = !(snap.error_flags & (ERROR_FLAG_TEMPERATURE | ERROR_FLAG_HUMIDITY));
            bool water_ok = !(snap.error_flags & ERROR_FLAG_WATER_LEVEL);

            /* Water state: only update when water sensor is valid */
            if (water_ok) {
                water_state_t next_water = water_state_update(water_state,
                                                              snap.water_level);
                if (next_water != water_state) {
                    ESP_LOGI(TAG, "Water state %u -> %u (water=%u)",
                             (unsigned)water_state, (unsigned)next_water,
                             snap.water_level);
                    water_state = next_water;
                }
            }

            /* Light state: only update when LDR is valid; last known state held on failure */
            if (ldr_ok) {
                light_state_t candidate = light_state_update(light_state,
                                                             snap.photoresistor);
                if (candidate != light_state) {
                    if (++light_confirm_ticks >= (LDR_CONFIRM_MS / CONTROL_TASK_PERIOD_MS)) {
                        light_state = candidate;
                        light_confirm_ticks = 0;
                        alarm_hold_ms  = 0;
                        night_guard_ms = (light_state == LIGHT_STATE_NIGHT)
                                         ? NIGHT_ALARM_GUARD_MS : 0u;
                        ESP_LOGI(TAG, "Light -> %s (ldr=%u)",
                                 light_state == LIGHT_STATE_DAY ? "DAY" : "NIGHT",
                                 snap.photoresistor);
                    }
                } else {
                    light_confirm_ticks = 0;
                }
            }

            /* Fan: temperature-dependent; off when sensor unavailable */
            if (temp_ok) {
                profile_thresholds_t thresholds = thresholds_for_profile(profile);
                cmd.fan_speed = fan_speed_for_temperature(snap.temperature, thresholds);
            } else {
                cmd.fan_speed = 0u;
            }

            /* LED and servo follow light_state (last valid value when LDR failing) */
            cmd.led_on        = (light_state == LIGHT_STATE_NIGHT) ? 1u : 0u;
            cmd.servo_position = (light_state == LIGHT_STATE_NIGHT) ?
                                 SERVO_NIGHT_POSITION : SERVO_DAY_POSITION;

            /* Pump: button is digital GPIO, always valid when data_fresh */
            cmd.pump_on = (snap.button_pressed != 0u) ? 1u : 0u;

            cmd.display_mode = (uint8_t)water_state;

            /* Guard countdown: alarm is suppressed for NIGHT_ALARM_GUARD_MS after
             * each NIGHT transition so the servo has time to settle. */
            if (night_guard_ms >= CONTROL_TASK_PERIOD_MS) {
                night_guard_ms -= CONTROL_TASK_PERIOD_MS;
            } else {
                night_guard_ms = 0;
            }

            /* Night alarm: PIR only — ultrasonic is excluded because the closed
             * servo hatch permanently sits in the sensor's field of view at night. */
            bool alarm_armed = (light_state == LIGHT_STATE_NIGHT) && (night_guard_ms == 0);
            if (alarm_armed && (snap.pir_detected != 0u)) {
                if (alarm_hold_ms == 0) {
                    ESP_LOGI(TAG, "Night alarm: PIR triggered");
                }
                alarm_hold_ms = NIGHT_ALARM_HOLD_MS;
            } else if (alarm_hold_ms >= CONTROL_TASK_PERIOD_MS) {
                alarm_hold_ms -= CONTROL_TASK_PERIOD_MS;
            } else {
                alarm_hold_ms = 0;
            }
            cmd.buzzer_on = (alarm_hold_ms > 0) ? 1u : 0u;
        } else {
            /* UART data stale: safe state for all actuators */
            alarm_hold_ms      = 0;
            cmd.fan_speed      = 0u;
            cmd.led_on         = 0u;
            cmd.servo_position = SERVO_DAY_POSITION;
            cmd.pump_on        = 0u;
            cmd.buzzer_on      = 0u;
            cmd.display_mode   = SYS_STATE_REFILL;
        }

        if (profile != last_profile) {
            ESP_LOGI(TAG, "Profile -> %s", profile_name(profile));
            last_profile = profile;
        }

        if (water_state != last_water_state) {
            g_system_state = (system_state_t)water_state;
            last_water_state = water_state;
        }

        if ((uint32_t)(now_ms - last_log_ms) >= CONTROL_LOG_INTERVAL_MS) {
            char s_water[8], s_ldr[8], s_temp[10];
            if (snap.error_flags & ERROR_FLAG_WATER_LEVEL)
                snprintf(s_water, sizeof(s_water), "ERR");
            else
                snprintf(s_water, sizeof(s_water), "%u", snap.water_level);
            if (snap.error_flags & ERROR_FLAG_LDR)
                snprintf(s_ldr, sizeof(s_ldr), "ERR");
            else
                snprintf(s_ldr, sizeof(s_ldr), "%u", snap.photoresistor);
            if (snap.error_flags & ERROR_FLAG_TEMPERATURE)
                snprintf(s_temp, sizeof(s_temp), "ERR");
            else
                snprintf(s_temp, sizeof(s_temp), "%d.%dC",
                         snap.temperature / 10,
                         snap.temperature < 0 ? -(snap.temperature % 10) :
                                                (snap.temperature % 10));
            ESP_LOGI(TAG,
                     "UC3 profile=%s water=%s ldr=%s temp=%s pir=%u -> fan=%u pump=%u led=%u servo=%u alarm=%u",
                     profile_name(profile), s_water, s_ldr, s_temp,
                     snap.pir_detected,
                     cmd.fan_speed, cmd.pump_on, cmd.led_on,
                     cmd.servo_position, cmd.buzzer_on);
            last_log_ms = now_ms;
        }

        if (xSemaphoreTake(g_tx_cmd_mutex, portMAX_DELAY) == pdTRUE) {
            g_tx_actuator_cmd = cmd;
            xSemaphoreGive(g_tx_cmd_mutex);
        }

        g_system_state = (system_state_t)cmd.display_mode;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
    }
}
