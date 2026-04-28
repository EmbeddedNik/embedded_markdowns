/*
 * @file    sensor_task.c
 * @brief   Reads all sensors cyclically; applies plausibility checks;
 *          publishes results to g_sensor_data (mutex-protected).
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#include "sensor_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "rom/ets_sys.h"

#include <string.h>
#include <math.h>

static const char *TAG = "sensor_task";

/* ── Shared globals ──────────────────────────────────────────────── */
sensor_data_t     g_sensor_data;
SemaphoreHandle_t g_sensor_mutex;

/* ── Plausibility helpers ────────────────────────────────────────── */
/* Returns true when value is within [lo, hi] */
static inline bool in_range_f(float v, float lo, float hi)
{
    return (v >= lo && v <= hi);
}

/* ── ADC ─────────────────────────────────────────────────────────── */
static void adc_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SENSOR_ADC_SOIL,        ADC_ATTEN_DB_11);
    adc1_config_channel_atten(SENSOR_ADC_WATER_LEVEL, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(SENSOR_ADC_LDR,         ADC_ATTEN_DB_11);
    adc1_config_channel_atten(SENSOR_ADC_STEAM,       ADC_ATTEN_DB_11);
}

/* Read one ADC1 channel; returns -1 on driver error. */
static int adc_read_raw(adc1_channel_t ch)
{
    return adc1_get_raw(ch);
}

/* ── Ultrasonic HC-SR04 ───────────────────────────────────────────── */
static void us_gpio_init(void)
{
    gpio_config_t trig_cfg = {
        .pin_bit_mask = (1ULL << SENSOR_PIN_US_TRIG),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&trig_cfg);
    gpio_set_level(SENSOR_PIN_US_TRIG, 0);

    gpio_config_t echo_cfg = {
        .pin_bit_mask = (1ULL << SENSOR_PIN_US_ECHO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&echo_cfg);
}

/*
 * Blocking HC-SR04 measurement.  Returns distance in cm, or -1.0 on
 * timeout (object out of range or sensor absent).
 * Max blocking time ≈ 10 ms (echo timeout guard).
 */
static float us_measure_cm(void)
{
    const int64_t ECHO_TIMEOUT_US = 10000; /* limit to ~1.7 m */

    /* 10 µs trigger pulse */
    gpio_set_level(SENSOR_PIN_US_TRIG, 1);
    ets_delay_us(10);
    gpio_set_level(SENSOR_PIN_US_TRIG, 0);

    /* Wait for echo rising edge */
    int64_t t_start = esp_timer_get_time();
    while (!gpio_get_level(SENSOR_PIN_US_ECHO)) {
        if (esp_timer_get_time() - t_start > ECHO_TIMEOUT_US) {
            return -1.0f;
        }
    }

    /* Measure echo pulse width */
    int64_t t_echo_start = esp_timer_get_time();
    while (gpio_get_level(SENSOR_PIN_US_ECHO)) {
        if (esp_timer_get_time() - t_echo_start > ECHO_TIMEOUT_US) {
            return -1.0f;
        }
    }
    int64_t pulse_us = esp_timer_get_time() - t_echo_start;

    /* distance = pulse_us * speed_of_sound / 2 = pulse_us * 0.034 / 2 */
    return (float)pulse_us * 0.017f;
}

/* ── DHT (DHT11 protocol) ────────────────────────────────────────── */
static void dht_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << SENSOR_PIN_DHT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

/*
 * Reads temperature and humidity from DHT11.
 * Must be called at most once per second.
 * Returns ESP_OK on success, ESP_FAIL on checksum or timeout error.
 *
 * Note: timing is sensitive to interrupt jitter.  Occasional failures
 * are handled by keeping the last valid value in the caller.
 */
static esp_err_t dht_read(float *temperature, float *humidity)
{
    const int64_t BIT_TIMEOUT_US  = 100;
    const int64_t BIT_ONE_THRESH  = 40;  /* µs: HIGH > 40 µs → bit=1 */

    uint8_t data[5] = {0};

    /* Send start: pull LOW for 20 ms, then release */
    gpio_set_direction(SENSOR_PIN_DHT, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_PIN_DHT, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(SENSOR_PIN_DHT, 1);
    ets_delay_us(40);
    gpio_set_direction(SENSOR_PIN_DHT, GPIO_MODE_INPUT);

    /* Wait for sensor to pull LOW (~80 µs) */
    int64_t t = esp_timer_get_time();
    while (gpio_get_level(SENSOR_PIN_DHT)) {
        if (esp_timer_get_time() - t > BIT_TIMEOUT_US) return ESP_FAIL;
    }
    /* Wait for sensor HIGH (~80 µs) */
    t = esp_timer_get_time();
    while (!gpio_get_level(SENSOR_PIN_DHT)) {
        if (esp_timer_get_time() - t > BIT_TIMEOUT_US) return ESP_FAIL;
    }
    t = esp_timer_get_time();
    while (gpio_get_level(SENSOR_PIN_DHT)) {
        if (esp_timer_get_time() - t > BIT_TIMEOUT_US) return ESP_FAIL;
    }

    /* Read 40 data bits */
    for (int i = 0; i < 40; i++) {
        /* Each bit begins with ~50 µs LOW */
        t = esp_timer_get_time();
        while (!gpio_get_level(SENSOR_PIN_DHT)) {
            if (esp_timer_get_time() - t > BIT_TIMEOUT_US) return ESP_FAIL;
        }
        /* Measure HIGH duration to distinguish 0 (26–28 µs) from 1 (70 µs) */
        t = esp_timer_get_time();
        while (gpio_get_level(SENSOR_PIN_DHT)) {
            if (esp_timer_get_time() - t > BIT_TIMEOUT_US) return ESP_FAIL;
        }
        int64_t high_us = esp_timer_get_time() - t;
        data[i / 8] <<= 1;
        if (high_us > BIT_ONE_THRESH) {
            data[i / 8] |= 1;
        }
    }

    /* Verify checksum */
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (data[4] != (checksum & 0xFF)) {
        return ESP_FAIL;
    }

    *humidity    = (float)data[0] + (float)data[1] * 0.1f;
    *temperature = (float)data[2] + (float)data[3] * 0.1f;
    return ESP_OK;
}

/* ── GPIO digital sensors ────────────────────────────────────────── */
static void digital_sensors_init(void)
{
    gpio_config_t pir_cfg = {
        .pin_bit_mask = (1ULL << SENSOR_PIN_PIR),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pir_cfg);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << SENSOR_PIN_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
}

/* ── Public init ─────────────────────────────────────────────────── */
void sensor_task_init(void)
{
    memset(&g_sensor_data, 0, sizeof(g_sensor_data));
    g_sensor_mutex = xSemaphoreCreateMutex();
    configASSERT(g_sensor_mutex != NULL);

    adc_init();
    us_gpio_init();
    dht_gpio_init();
    digital_sensors_init();
}

/* ── Task loop ───────────────────────────────────────────────────── */
void sensor_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t  last_wake        = xTaskGetTickCount();
    uint32_t    log_counter      = 0;
    uint32_t    dht_counter      = 0;

    /* Last-valid fallback values */
    float last_distance  = 0.0f;
    float last_temp      = 20.0f;
    float last_humidity  = 50.0f;

    const uint32_t LOG_EVERY     = SENSOR_LOG_INTERVAL_MS / SENSOR_TASK_PERIOD_MS;
    const uint32_t DHT_EVERY     = 1000 / SENSOR_TASK_PERIOD_MS; /* 1 s */

    for (;;) {
        esp_task_wdt_reset();

        sensor_data_t local = {0};

        /* ── ADC sensors ─────────────────────────────────────────── */
        local.soil_raw        = adc_read_raw(SENSOR_ADC_SOIL);
        local.water_level_raw = adc_read_raw(SENSOR_ADC_WATER_LEVEL);
        local.ldr_raw         = adc_read_raw(SENSOR_ADC_LDR);
        local.steam_raw       = adc_read_raw(SENSOR_ADC_STEAM);

        local.err_soil        = (local.soil_raw        < 0);
        local.err_water_level = (local.water_level_raw < 0);
        local.err_ldr         = (local.ldr_raw         < 0);
        local.err_steam       = (local.steam_raw       < 0);

        if (local.err_soil)        { ESP_LOGW(TAG, "ADC error: soil");        local.soil_raw        = 0; }
        if (local.err_water_level) { ESP_LOGW(TAG, "ADC error: water_level"); local.water_level_raw = 0; }
        if (local.err_ldr)         { ESP_LOGW(TAG, "ADC error: LDR");         local.ldr_raw         = 0; }
        if (local.err_steam)       { ESP_LOGW(TAG, "ADC error: steam");        local.steam_raw       = 0; }

        /* ── Digital sensors ─────────────────────────────────────── */
        local.pir_detected   = (gpio_get_level(SENSOR_PIN_PIR)    == 1);
        local.button_pressed = (gpio_get_level(SENSOR_PIN_BUTTON) == 0); /* pull-up: LOW=pressed */

        /* ── Ultrasonic ──────────────────────────────────────────── */
        float dist = us_measure_cm();
        if (dist < 0.0f || !in_range_f(dist, SENSOR_DISTANCE_CM_MIN, SENSOR_DISTANCE_CM_MAX)) {
            local.err_distance = true;
            local.distance_cm  = last_distance;
            if (dist >= 0.0f) ESP_LOGW(TAG, "Ultrasonic out of range: %.1f cm", dist);
        } else {
            local.err_distance = false;
            local.distance_cm  = dist;
            last_distance      = dist;
        }

        /* ── DHT (once per second) ───────────────────────────────── */
        if (dht_counter == 0) {
            float temp = 0.0f, hum = 0.0f;
            if (dht_read(&temp, &hum) == ESP_OK) {
                if (in_range_f(temp, SENSOR_TEMP_C_MIN, SENSOR_TEMP_C_MAX)) {
                    local.err_temperature = false;
                    local.temperature_c   = temp;
                    last_temp             = temp;
                } else {
                    local.err_temperature = true;
                    local.temperature_c   = last_temp;
                    ESP_LOGW(TAG, "Temperature out of range: %.1f C", temp);
                }
                if (in_range_f(hum, SENSOR_HUMIDITY_MIN, SENSOR_HUMIDITY_MAX)) {
                    local.err_humidity = false;
                    local.humidity_pct = hum;
                    last_humidity      = hum;
                } else {
                    local.err_humidity = true;
                    local.humidity_pct = last_humidity;
                    ESP_LOGW(TAG, "Humidity out of range: %.1f %%", hum);
                }
            } else {
                /* Keep last valid values on read failure */
                local.err_temperature = true;
                local.err_humidity    = true;
                local.temperature_c   = last_temp;
                local.humidity_pct    = last_humidity;
                ESP_LOGW(TAG, "DHT read failed");
            }
        } else {
            /* Carry forward DHT values between measurements */
            if (xSemaphoreTake(g_sensor_mutex, 0) == pdTRUE) {
                local.temperature_c   = g_sensor_data.temperature_c;
                local.humidity_pct    = g_sensor_data.humidity_pct;
                local.err_temperature = g_sensor_data.err_temperature;
                local.err_humidity    = g_sensor_data.err_humidity;
                xSemaphoreGive(g_sensor_mutex);
            } else {
                local.temperature_c = last_temp;
                local.humidity_pct  = last_humidity;
            }
        }
        dht_counter = (dht_counter + 1) % DHT_EVERY;

        /* ── Publish to shared struct ─────────────────────────────── */
        if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_sensor_data = local;
            xSemaphoreGive(g_sensor_mutex);
        } else {
            ESP_LOGW(TAG, "Mutex timeout – sensor data not updated");
        }

        /* ── Debug log every 500 ms ──────────────────────────────── */
        if (log_counter == 0) {
            ESP_LOGI(TAG,
                     "soil=%d wlvl=%d ldr=%d steam=%d | pir=%d btn=%d "
                     "| dist=%.1fcm | temp=%.1fC hum=%.1f%% "
                     "| err=[s:%d w:%d l:%d st:%d d:%d t:%d h:%d]",
                     local.soil_raw, local.water_level_raw,
                     local.ldr_raw,  local.steam_raw,
                     local.pir_detected, local.button_pressed,
                     local.distance_cm,
                     local.temperature_c, local.humidity_pct,
                     local.err_soil,         local.err_water_level,
                     local.err_ldr,          local.err_steam,
                     local.err_distance,     local.err_temperature,
                     local.err_humidity);
        }
        log_counter = (log_counter + 1) % LOG_EVERY;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}
