/*
 * @file    control_logic.h
 * @brief   Pure control logic: FSM state types, fan speed computation.
 *          No FreeRTOS or ESP-IDF dependencies — host-testable.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include <stdint.h>
#include "control_task.h"   /* system_profile_t */

typedef enum {
    WATER_STATE_OK     = 0,
    WATER_STATE_REFILL = 1,
} water_state_t;

typedef enum {
    LIGHT_STATE_DAY   = 0,
    LIGHT_STATE_NIGHT = 1,
} light_state_t;

typedef struct {
    int16_t fan_low_d10;
    int16_t fan_high_d10;
} profile_thresholds_t;

const char           *profile_name(system_profile_t profile);
profile_thresholds_t  thresholds_for_profile(system_profile_t profile);
water_state_t         water_state_update(water_state_t current, uint16_t water_level);
light_state_t         light_state_update(light_state_t current, uint16_t ldr_adc);
uint8_t               fan_speed_for_temperature(int16_t temp_d10, profile_thresholds_t thresholds);

#endif /* CONTROL_LOGIC_H */
