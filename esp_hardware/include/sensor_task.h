/*
 * @file    sensor_task.h
 * @brief   Sensor task: data types, shared state, and public API
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ── GPIO pin definitions ────────────────────────────────────────── */
#define SENSOR_PIN_PIR          23
#define SENSOR_PIN_BUTTON       5
#define SENSOR_PIN_US_TRIG      12
#define SENSOR_PIN_US_ECHO      13
#define SENSOR_PIN_DHT          17

/* ── ADC channel mapping (ESP32 ADC1) ───────────────────────────── */
/* GPIO32 = ADC1_CH4 | GPIO33 = ADC1_CH5 | GPIO34 = ADC1_CH6 | GPIO35 = ADC1_CH7 */
#define SENSOR_ADC_SOIL         ADC1_CHANNEL_4   /* io32 */
#define SENSOR_ADC_WATER_LEVEL  ADC1_CHANNEL_5   /* io33 */
#define SENSOR_ADC_LDR          ADC1_CHANNEL_6   /* io34 */
#define SENSOR_ADC_STEAM        ADC1_CHANNEL_7   /* io35 */

/* ── Plausibility limits ─────────────────────────────────────────── */
#define SENSOR_DISTANCE_CM_MIN  2.0f
#define SENSOR_DISTANCE_CM_MAX  400.0f
#define SENSOR_TEMP_C_MIN       -40.0f
#define SENSOR_TEMP_C_MAX       80.0f
#define SENSOR_HUMIDITY_MIN     0.0f
#define SENSOR_HUMIDITY_MAX     100.0f

/* ── Shared sensor data (protected by g_sensor_mutex) ───────────── */
typedef struct {
    /* ADC sensors (raw 12-bit, 0–4095) */
    int     soil_raw;
    int     water_level_raw;
    int     ldr_raw;
    int     steam_raw;

    /* Digital sensors */
    bool    pir_detected;
    bool    button_pressed;

    /* Ultrasonic (cm) */
    float   distance_cm;

    /* DHT (temperature in °C, relative humidity in %) */
    float   temperature_c;
    float   humidity_pct;

    /* Per-sensor error flags (set when plausibility check fails) */
    bool    err_soil;
    bool    err_water_level;
    bool    err_ldr;
    bool    err_steam;
    bool    err_distance;
    bool    err_temperature;
    bool    err_humidity;
} sensor_data_t;

/* ── Globals defined in sensor_task.c ───────────────────────────── */
extern sensor_data_t    g_sensor_data;
extern SemaphoreHandle_t g_sensor_mutex;

/* ── Public API ──────────────────────────────────────────────────── */
void sensor_task_init(void);
void sensor_task(void *arg);

#endif /* SENSOR_TASK_H */
