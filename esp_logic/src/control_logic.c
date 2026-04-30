/*
 * @file    control_logic.c
 * @brief   Pure control logic: FSM updates and fan speed computation.
 *          No FreeRTOS or ESP-IDF dependencies — host-testable.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#include "control_logic.h"
#include "task_config.h"

const char *profile_name(system_profile_t profile)
{
    switch (profile) {
        case SYSTEM_PROFILE_ECO:         return "ECO";
        case SYSTEM_PROFILE_NORMAL:      return "NORMAL";
        case SYSTEM_PROFILE_PERFORMANCE: return "PERFORMANCE";
        default:                         return "UNKNOWN";
    }
}

profile_thresholds_t thresholds_for_profile(system_profile_t profile)
{
    switch (profile) {
        case SYSTEM_PROFILE_ECO:
            return (profile_thresholds_t){
                .fan_low_d10  = ECO_FAN_LOW_TEMP_D10,
                .fan_high_d10 = ECO_FAN_HIGH_TEMP_D10,
            };
        case SYSTEM_PROFILE_PERFORMANCE:
            return (profile_thresholds_t){
                .fan_low_d10  = PERF_FAN_LOW_TEMP_D10,
                .fan_high_d10 = PERF_FAN_HIGH_TEMP_D10,
            };
        case SYSTEM_PROFILE_NORMAL:
        default:
            return (profile_thresholds_t){
                .fan_low_d10  = NORMAL_FAN_LOW_TEMP_D10,
                .fan_high_d10 = NORMAL_FAN_HIGH_TEMP_D10,
            };
    }
}

water_state_t water_state_update(water_state_t current, uint16_t water_level)
{
    if (current == WATER_STATE_REFILL) {
        return (water_level >= WATER_REFILL_EXIT) ? WATER_STATE_OK : WATER_STATE_REFILL;
    }
    return (water_level <= WATER_REFILL_ENTER) ? WATER_STATE_REFILL : WATER_STATE_OK;
}

light_state_t light_state_update(light_state_t current, uint16_t ldr_adc)
{
    if (current == LIGHT_STATE_NIGHT) {
        return (ldr_adc >= LDR_DAY_ENTER_ADC) ? LIGHT_STATE_DAY : LIGHT_STATE_NIGHT;
    }
    return (ldr_adc <= LDR_NIGHT_ENTER_ADC) ? LIGHT_STATE_NIGHT : LIGHT_STATE_DAY;
}

uint8_t fan_speed_for_temperature(int16_t temp_d10, profile_thresholds_t thresholds)
{
    if (temp_d10 >= thresholds.fan_high_d10) {
        return 2u;
    }
    if (temp_d10 >= thresholds.fan_low_d10) {
        return 1u;
    }
    return 0u;
}
