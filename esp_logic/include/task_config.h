/*
 * @file    task_config.h
 * @brief   FreeRTOS task configuration and UC3 thresholds for esp_logic.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

/* control_task */
#define CONTROL_TASK_STACK_SIZE     4096
#define CONTROL_TASK_PRIORITY       4
#define CONTROL_TASK_PERIOD_MS      10

/* comm_task */
#define COMM_TASK_STACK_SIZE        4096
#define COMM_TASK_PRIORITY          5
#define COMM_TASK_PERIOD_MS         10

/* serial_task: USB/UART0 user profile input */
#define SERIAL_TASK_STACK_SIZE      3072
#define SERIAL_TASK_PRIORITY        3
#define SERIAL_TASK_PERIOD_MS       20

/* display_task */
#define DISPLAY_TASK_STACK_SIZE     4096
#define DISPLAY_TASK_PRIORITY       2
#define DISPLAY_TASK_PERIOD_MS      100

/* monitor_task */
#define MONITOR_TASK_STACK_SIZE     3072
#define MONITOR_TASK_PRIORITY       4
#define MONITOR_TASK_PERIOD_MS      10

/* watchdog_task */
#define WATCHDOG_TASK_STACK_SIZE    2048
#define WATCHDOG_TASK_PRIORITY      6
#define WATCHDOG_TASK_PERIOD_MS     1000

#define TWDT_TIMEOUT_MS             5000
#define SENSOR_STALE_MS             600
#define HWM_LOG_INTERVAL_CYCLES     10
#define TASK_MIN_STACK_HWM          512

/* UC3 water state thresholds (ADC raw counts, 12-bit / 0-4095)
 * OK: level > 1000 | REFILL: level <= 1000
 * 20-count hysteresis prevents chattering at the boundary */
#define WATER_REFILL_ENTER          1000
#define WATER_REFILL_EXIT           1020

/* LDR day/night detection with hysteresis */
#define LDR_NIGHT_ENTER_ADC         1100
#define LDR_DAY_ENTER_ADC           1300
#define LDR_CONFIRM_MS              1000   /* candidate state must hold for 1 s before committing */

/* Servo position follows day/night state */
#define SERVO_DAY_POSITION          0
#define SERVO_NIGHT_POSITION        100

/* Profile-specific fan thresholds, temperature in deg C * 10 */
#define ECO_FAN_LOW_TEMP_D10        280
#define ECO_FAN_HIGH_TEMP_D10       320
#define NORMAL_FAN_LOW_TEMP_D10     250
#define NORMAL_FAN_HIGH_TEMP_D10    280
#define PERF_FAN_LOW_TEMP_D10       230
#define PERF_FAN_HIGH_TEMP_D10      260

/* Critical water buzzer: 200 ms beep every 2 s */
#define WATER_BUZZER_PERIOD_CYCLES  200U
#define WATER_BUZZER_ON_CYCLES      20U

#define CONTROL_LOG_INTERVAL_MS     1000

#endif /* TASK_CONFIG_H */
