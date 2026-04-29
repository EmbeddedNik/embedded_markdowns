/*
 * @file    control_task.c
 * @brief   UC3 profile-based load steering for esp_logic.
 *          No PI control: pump is a direct/manual load, water state only
 *          notifies via display and buzzer.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "control_task.h"
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

typedef enum {
    WATER_STATE_OK = 0,
    WATER_STATE_WARNING = 1,
    WATER_STATE_CRITICAL = 2,
} water_state_t;

typedef enum {
    LIGHT_STATE_DAY = 0,
    LIGHT_STATE_NIGHT = 1,
} light_state_t;

typedef struct {
    int16_t fan_low_d10;
    int16_t fan_high_d10;
} profile_thresholds_t;

static const char *profile_name(system_profile_t profile)
{
    switch (profile) {
        case SYSTEM_PROFILE_ECO:         return "ECO";
        case SYSTEM_PROFILE_NORMAL:      return "NORMAL";
        case SYSTEM_PROFILE_PERFORMANCE: return "PERFORMANCE";
        default:                         return "UNKNOWN";
    }
}

static profile_thresholds_t thresholds_for_profile(system_profile_t profile)
{
    switch (profile) {
        case SYSTEM_PROFILE_ECO:
            return (profile_thresholds_t){
                .fan_low_d10 = ECO_FAN_LOW_TEMP_D10,
                .fan_high_d10 = ECO_FAN_HIGH_TEMP_D10,
            };
        case SYSTEM_PROFILE_PERFORMANCE:
            return (profile_thresholds_t){
                .fan_low_d10 = PERF_FAN_LOW_TEMP_D10,
                .fan_high_d10 = PERF_FAN_HIGH_TEMP_D10,
            };
        case SYSTEM_PROFILE_NORMAL:
        default:
            return (profile_thresholds_t){
                .fan_low_d10 = NORMAL_FAN_LOW_TEMP_D10,
                .fan_high_d10 = NORMAL_FAN_HIGH_TEMP_D10,
            };
    }
}

static water_state_t water_state_update(water_state_t current, uint16_t water_level)
{
    switch (current) {
        case WATER_STATE_CRITICAL:
            if (water_level >= WATER_CRITICAL_EXIT) {
                return WATER_STATE_WARNING;
            }
            return WATER_STATE_CRITICAL;

        case WATER_STATE_WARNING:
            if (water_level <= WATER_CRITICAL_ENTER) {
                return WATER_STATE_CRITICAL;
            }
            if (water_level >= WATER_WARNING_EXIT) {
                return WATER_STATE_OK;
            }
            return WATER_STATE_WARNING;

        case WATER_STATE_OK:
        default:
            if (water_level <= WATER_CRITICAL_ENTER) {
                return WATER_STATE_CRITICAL;
            }
            if (water_level <= WATER_WARNING_ENTER) {
                return WATER_STATE_WARNING;
            }
            return WATER_STATE_OK;
    }
}

static light_state_t light_state_update(light_state_t current, uint16_t ldr_adc)
{
    if (current == LIGHT_STATE_NIGHT) {
        return (ldr_adc >= LDR_DAY_ENTER_ADC) ? LIGHT_STATE_DAY : LIGHT_STATE_NIGHT;
    }
    return (ldr_adc <= LDR_NIGHT_ENTER_ADC) ? LIGHT_STATE_NIGHT : LIGHT_STATE_DAY;
}

static uint8_t fan_speed_for_temperature(int16_t temp_d10,
                                         profile_thresholds_t thresholds)
{
    if (temp_d10 >= thresholds.fan_high_d10) {
        return 2u;
    }
    if (temp_d10 >= thresholds.fan_low_d10) {
        return 1u;
    }
    return 0u;
}

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
    uint32_t last_log_ms = 0;
    uint32_t buzzer_cycle = 0;

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

            if (water_state == WATER_STATE_CRITICAL) {
                buzzer_cycle = (buzzer_cycle + 1u) % WATER_BUZZER_PERIOD_CYCLES;
                cmd.buzzer_on = (buzzer_cycle < WATER_BUZZER_ON_CYCLES) ? 1u : 0u;
            } else {
                buzzer_cycle = 0;
                cmd.buzzer_on = 0u;
            }
        } else {
            /* UART data stale: safe state for all actuators */
            cmd.fan_speed      = 0u;
            cmd.led_on         = 0u;
            cmd.servo_position = SERVO_DAY_POSITION;
            cmd.pump_on        = 0u;
            cmd.buzzer_on      = 0u;
            cmd.display_mode   = SYS_STATE_WARNING;
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
            ESP_LOGI(TAG,
                     "UC3 profile=%s water=%u ldr=%u temp=%d.%dC -> fan=%u pump=%u led=%u servo=%u buzzer=%u",
                     profile_name(profile), snap.water_level, snap.photoresistor,
                     snap.temperature / 10,
                     snap.temperature < 0 ? -(snap.temperature % 10) :
                                            (snap.temperature % 10),
                     cmd.fan_speed, cmd.pump_on, cmd.led_on,
                     cmd.servo_position, cmd.buzzer_on);
            last_log_ms = now_ms;
        }

        if (xSemaphoreTake(g_tx_cmd_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_tx_actuator_cmd = cmd;
            xSemaphoreGive(g_tx_cmd_mutex);
        } else {
            ESP_LOGW(TAG, "TX cmd mutex timeout");
        }

        g_system_state = (system_state_t)cmd.display_mode;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
    }
}
