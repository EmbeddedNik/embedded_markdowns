/*
 * @file    test_control_logic.c
 * @brief   Unit tests for water/light FSMs and fan speed computation.
 *          Run with: pio test -e native
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-30
 */

#include "unity.h"
#include "control_logic.h"
#include "task_config.h"

void setUp(void)    {}
void tearDown(void) {}

/* ── Water state machine ─────────────────────────────────────────── */

void test_water_ok_transitions_to_refill_at_threshold(void)
{
    /* In OK state, level at or below WATER_REFILL_ENTER → must enter REFILL */
    water_state_t result = water_state_update(WATER_STATE_OK, WATER_REFILL_ENTER);
    TEST_ASSERT_EQUAL(WATER_STATE_REFILL, result);
}

void test_water_ok_stays_ok_above_threshold(void)
{
    /* In OK state, level clearly above threshold → stays OK */
    water_state_t result = water_state_update(WATER_STATE_OK, WATER_REFILL_ENTER + 1);
    TEST_ASSERT_EQUAL(WATER_STATE_OK, result);
}

void test_water_hysteresis_stays_in_refill(void)
{
    /* In REFILL state, level is above ENTER but still below EXIT → must stay REFILL.
     * This verifies the hysteresis gap prevents immediate re-entry into OK. */
    water_state_t result = water_state_update(WATER_STATE_REFILL, WATER_REFILL_EXIT - 1);
    TEST_ASSERT_EQUAL(WATER_STATE_REFILL, result);
}

void test_water_exits_refill_at_exit_threshold(void)
{
    /* In REFILL state, level reaches EXIT threshold → transitions back to OK */
    water_state_t result = water_state_update(WATER_STATE_REFILL, WATER_REFILL_EXIT);
    TEST_ASSERT_EQUAL(WATER_STATE_OK, result);
}

/* ── Light state machine ─────────────────────────────────────────── */

void test_light_day_transitions_to_night(void)
{
    /* In DAY state, LDR at or below NIGHT threshold → must enter NIGHT */
    light_state_t result = light_state_update(LIGHT_STATE_DAY, LDR_NIGHT_ENTER_ADC);
    TEST_ASSERT_EQUAL(LIGHT_STATE_NIGHT, result);
}

void test_light_night_hysteresis_stays_night(void)
{
    /* In NIGHT state, LDR is above NIGHT threshold but below DAY threshold → stays NIGHT.
     * This verifies the hysteresis gap prevents chattering between states. */
    light_state_t result = light_state_update(LIGHT_STATE_NIGHT, LDR_DAY_ENTER_ADC - 1);
    TEST_ASSERT_EQUAL(LIGHT_STATE_NIGHT, result);
}

void test_light_night_exits_at_day_threshold(void)
{
    /* In NIGHT state, LDR reaches DAY threshold → transitions back to DAY */
    light_state_t result = light_state_update(LIGHT_STATE_NIGHT, LDR_DAY_ENTER_ADC);
    TEST_ASSERT_EQUAL(LIGHT_STATE_DAY, result);
}

/* ── Fan speed ───────────────────────────────────────────────────── */

void test_fan_speed_all_levels_normal_profile(void)
{
    profile_thresholds_t t = thresholds_for_profile(SYSTEM_PROFILE_NORMAL);

    /* Below low threshold → fan off */
    TEST_ASSERT_EQUAL(0u, fan_speed_for_temperature(t.fan_low_d10 - 1, t));

    /* At low threshold → fan low */
    TEST_ASSERT_EQUAL(1u, fan_speed_for_temperature(t.fan_low_d10, t));

    /* At high threshold → fan high */
    TEST_ASSERT_EQUAL(2u, fan_speed_for_temperature(t.fan_high_d10, t));
}

void test_fan_eco_activates_later_than_performance(void)
{
    /* ECO profile conserves power: fan switches on at a higher temperature
     * than PERFORMANCE profile. */
    profile_thresholds_t eco  = thresholds_for_profile(SYSTEM_PROFILE_ECO);
    profile_thresholds_t perf = thresholds_for_profile(SYSTEM_PROFILE_PERFORMANCE);
    TEST_ASSERT_GREATER_THAN(perf.fan_low_d10, eco.fan_low_d10);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_water_ok_transitions_to_refill_at_threshold);
    RUN_TEST(test_water_ok_stays_ok_above_threshold);
    RUN_TEST(test_water_hysteresis_stays_in_refill);
    RUN_TEST(test_water_exits_refill_at_exit_threshold);

    RUN_TEST(test_light_day_transitions_to_night);
    RUN_TEST(test_light_night_hysteresis_stays_night);
    RUN_TEST(test_light_night_exits_at_day_threshold);

    RUN_TEST(test_fan_speed_all_levels_normal_profile);
    RUN_TEST(test_fan_eco_activates_later_than_performance);

    return UNITY_END();
}
