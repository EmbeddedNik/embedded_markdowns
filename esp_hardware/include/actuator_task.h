/*
 * @file    actuator_task.h
 * @brief   Actuator task: command types, queue handle, and public API
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#ifndef ACTUATOR_TASK_H
#define ACTUATOR_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ── GPIO pin definitions ────────────────────────────────────────── */
#define ACTUATOR_PIN_FAN_IN_N   18   /* H-bridge IN-  (direction / brake) */
#define ACTUATOR_PIN_FAN_IN_P   19   /* H-bridge IN+  (PWM speed control) */
#define ACTUATOR_PIN_SERVO      26
#define ACTUATOR_PIN_PUMP       25   /* 5 V relay, active HIGH */
#define ACTUATOR_PIN_BUZZER     16   /* active HIGH */
#define ACTUATOR_PIN_LED        27   /* active HIGH */

/* ── LEDC configuration ──────────────────────────────────────────── */
#define LEDC_TIMER_SERVO        LEDC_TIMER_0
#define LEDC_TIMER_FAN          LEDC_TIMER_1
#define LEDC_CHANNEL_SERVO      LEDC_CHANNEL_0
#define LEDC_CHANNEL_FAN        LEDC_CHANNEL_1

#define SERVO_FREQ_HZ           50
#define SERVO_RESOLUTION        LEDC_TIMER_16_BIT
/* 16-bit at 50 Hz: tick = 20 ms / 65535 ≈ 0.305 µs */
#define SERVO_DUTY_0DEG         3276    /* ~1 ms */
#define SERVO_DUTY_180DEG       6553    /* ~2 ms */

#define FAN_FREQ_HZ             1000
#define FAN_RESOLUTION          LEDC_TIMER_8_BIT   /* 0–255 */

/* ── Actuator command (sent via g_actuator_queue) ────────────────── */
typedef struct {
    bool    fan_enable;
    uint8_t fan_speed_pct;    /* 0–100 % */
    uint16_t servo_angle_deg; /* 0–180   */
    bool    pump_enable;
    bool    buzzer_enable;
    bool    led_enable;
} actuator_cmd_t;

/* ── Queue defined in actuator_task.c ───────────────────────────── */
extern QueueHandle_t g_actuator_queue;

/* ── Public API ──────────────────────────────────────────────────── */
void actuator_task_init(void);
void actuator_task(void *arg);

#endif /* ACTUATOR_TASK_H */
