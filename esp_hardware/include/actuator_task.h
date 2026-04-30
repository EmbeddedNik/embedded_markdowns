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
#define ACTUATOR_PIN_FAN_IN_N   18   /* H-bridge IN-, held LOW (unused direction input) */
#define ACTUATOR_PIN_FAN_IN_P   19   /* H-bridge IN+, PWM speed control */
#define ACTUATOR_PIN_SERVO      26
#define ACTUATOR_PIN_PUMP       25   /* 5 V relay */
#define ACTUATOR_PUMP_ACTIVE_LEVEL 1  /* pump relay is active HIGH */
#define ACTUATOR_PIN_BUZZER     16   /* active HIGH */
#define ACTUATOR_PIN_LED        27   /* active HIGH */

/* ── LEDC configuration ──────────────────────────────────────────── */
#define LEDC_TIMER_SERVO        LEDC_TIMER_0
#define LEDC_TIMER_FAN          LEDC_TIMER_1
#define LEDC_TIMER_BUZZER       LEDC_TIMER_2
#define LEDC_CHANNEL_SERVO      LEDC_CHANNEL_0
#define LEDC_CHANNEL_FAN        LEDC_CHANNEL_1
#define LEDC_CHANNEL_BUZZER     LEDC_CHANNEL_2

#define BUZZER_FREQ_HZ          1000
#define BUZZER_RESOLUTION       LEDC_TIMER_8_BIT
#define BUZZER_DUTY_ON          128    /* 50% duty – audible 1 kHz tone */

#define SERVO_FREQ_HZ           50
#define SERVO_RESOLUTION        LEDC_TIMER_16_BIT
/* 16-bit at 50 Hz: tick = 20 ms / 65535 ≈ 0.305 µs */
#define SERVO_DUTY_0DEG         1638    /* ~0.5 ms – SG90 minimum */
#define SERVO_DUTY_180DEG       7864    /* ~2.4 ms – SG90 maximum */
#define SERVO_CLOSED_ANGLE_DEG  75      /* calibrate: increase to move away from minimum stop */
#define SERVO_OPEN_ANGLE_DEG    180     /* calibrate: decrease to move away from maximum stop */
#define SERVO_HOLD_MS           250      /* ms PWM stays on; LEDC hardware drives pin 26 autonomously during this time */

#define FAN_FREQ_HZ             1000
#define FAN_RESOLUTION          LEDC_TIMER_8_BIT   /* 0-255 */
#define FAN_DUTY_ECO            50
#define FAN_DUTY_NORMAL         120
#define FAN_DUTY_PERFORMANCE    220

/* ── Internal hardware actuator command (sent via g_actuator_queue) ─ */
typedef struct {
    bool     fan_enable;
    uint8_t  fan_duty;         /* LEDC duty 0-255, equivalent to Arduino analogWrite */
    bool     servo_enable;
    uint16_t servo_angle_deg;  /* 0–180   */
    bool     pump_enable;
    bool     buzzer_enable;
    bool     led_enable;
    uint8_t  display_mode;     /* 0=normal,1=warning,2=critical,3=alarm */
    uint8_t  profile;          /* 0=eco,1=normal,2=performance */
} hw_actuator_cmd_t;

/* ── Queue defined in actuator_task.c ───────────────────────────── */
extern QueueHandle_t g_actuator_queue;

/* ── Public API ──────────────────────────────────────────────────── */
void actuator_task_init(void);
void actuator_task(void *arg);

#endif /* ACTUATOR_TASK_H */
