/*
 * @file    sensor_task.c
 * @brief   Reads all sensors cyclically; applies plausibility checks;
 *          publishes results to g_sensor_data (mutex-protected).
 *          ADC uses the ESP-IDF 5.x oneshot driver (esp_adc/adc_oneshot.h).
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "sensor_task.h"
#include "roc_detector.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "rom/ets_sys.h"

#include <string.h>
#include <math.h>

static const char *TAG = "sensor_task";

/* ── Shared globals ──────────────────────────────────────────────── */
hw_sensor_data_t  g_sensor_data;
SemaphoreHandle_t g_sensor_mutex;

/* ── ADC oneshot driver handle ───────────────────────────────────── */
static adc_oneshot_unit_handle_t s_adc1_handle;

/* ── RoC fault detectors (one per sensor, zero-initialised) ─────── */
static roc_detector_t s_roc_ldr   = {0};
static roc_detector_t s_roc_water = {0};

/*
 * LDR: a light switch in the same room can cause a large single-cycle jump
 * (~800 counts) that immediately stabilises.  The detector only declares a
 * fault after fault_confirm_n = 5 consecutive over-threshold readings (50 ms),
 * which a real lighting change never produces.
 *
 * Water level: even aggressive manual refilling causes at most ~100 counts
 * per 10 ms cycle (capacitive sensor + physical water rise).  A floating
 * input can jump 2000+ counts per cycle.
 */
static const roc_params_t ROC_LDR_PARAMS = {
    .delta_threshold  = 600,
    .fault_confirm_n  = 5,
    .recover_confirm_n = 3,
};

static const roc_params_t ROC_WATER_PARAMS = {
    .delta_threshold  = 300,
    .fault_confirm_n  = 3,
    .recover_confirm_n = 3,
};

/* ── Plausibility helpers ────────────────────────────────────────── */
static inline bool in_range_f(float v, float lo, float hi)
{
    return (v >= lo && v <= hi);
}

/* ── ADC ─────────────────────────────────────────────────────────── */
static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc1_handle));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,   /* 0–3.3 V range (DB_11 renamed DB_12 in IDF 5) */
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, SENSOR_ADC_WATER_LEVEL, &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, SENSOR_ADC_LDR,         &ch_cfg));
}

/* Read one ADC1 channel; returns -1 on driver error. */
static int adc_read_raw(adc_channel_t ch)
{
    int raw = -1;
    if (adc_oneshot_read(s_adc1_handle, ch, &raw) != ESP_OK) {
        return -1;
    }
    return raw;
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
 * Max blocking time ≈ 10 ms.
 */
static float us_measure_cm(void)
{
    const int64_t ECHO_TIMEOUT_US = 10000;

    gpio_set_level(SENSOR_PIN_US_TRIG, 1);
    ets_delay_us(10);
    gpio_set_level(SENSOR_PIN_US_TRIG, 0);

    int64_t t_start = esp_timer_get_time();
    while (!gpio_get_level(SENSOR_PIN_US_ECHO)) {
        if (esp_timer_get_time() - t_start > ECHO_TIMEOUT_US) return -1.0f;
    }

    int64_t t_echo_start = esp_timer_get_time();
    while (gpio_get_level(SENSOR_PIN_US_ECHO)) {
        if (esp_timer_get_time() - t_echo_start > ECHO_TIMEOUT_US) return -1.0f;
    }
    int64_t pulse_us = esp_timer_get_time() - t_echo_start;

    return (float)pulse_us * 0.017f;
}

/* ── DHT11 ───────────────────────────────────────────────────────── */
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

static esp_err_t dht_read(float *temperature, float *humidity)
{
    const int64_t BIT_TIMEOUT_US = 100;
    const int64_t BIT_ONE_THRESH = 40;

    uint8_t   data[5] = {0};
    esp_err_t err     = ESP_OK;

    /* Start signal: pull LOW 20 ms then release — preemption is safe here. */
    gpio_set_direction(SENSOR_PIN_DHT, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_PIN_DHT, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(SENSOR_PIN_DHT, 1);
    ets_delay_us(40);
    gpio_set_direction(SENSOR_PIN_DHT, GPIO_MODE_INPUT);

    /* Bit-bang timing windows are 26–70 µs; a context switch here corrupts
     * the reading.  Suspend the scheduler only — ISRs (UART, timers) keep
     * running so comm_task drains its buffer after xTaskResumeAll(). */
    vTaskSuspendAll();

    int64_t t = esp_timer_get_time();
    while (gpio_get_level(SENSOR_PIN_DHT))  { if (esp_timer_get_time()-t > BIT_TIMEOUT_US) { err = ESP_FAIL; goto done; } }
    t = esp_timer_get_time();
    while (!gpio_get_level(SENSOR_PIN_DHT)) { if (esp_timer_get_time()-t > BIT_TIMEOUT_US) { err = ESP_FAIL; goto done; } }
    t = esp_timer_get_time();
    while (gpio_get_level(SENSOR_PIN_DHT))  { if (esp_timer_get_time()-t > BIT_TIMEOUT_US) { err = ESP_FAIL; goto done; } }

    for (int i = 0; i < 40; i++) {
        t = esp_timer_get_time();
        while (!gpio_get_level(SENSOR_PIN_DHT)) { if (esp_timer_get_time()-t > BIT_TIMEOUT_US) { err = ESP_FAIL; goto done; } }
        t = esp_timer_get_time();
        while (gpio_get_level(SENSOR_PIN_DHT))  { if (esp_timer_get_time()-t > BIT_TIMEOUT_US) { err = ESP_FAIL; goto done; } }
        int64_t high_us = esp_timer_get_time() - t;
        data[i / 8] <<= 1;
        if (high_us > BIT_ONE_THRESH) data[i / 8] |= 1;
    }

done:
    xTaskResumeAll();   /* re-enable scheduler; pending context switches fire here */

    if (err != ESP_OK) return err;
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) return ESP_FAIL;

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

    TickType_t  last_wake   = xTaskGetTickCount();
    uint32_t    dht_counter = 0;
    uint32_t    log_counter = 0;

    float last_distance = 0.0f;
    float last_temp     = 20.0f;
    float last_humidity = 50.0f;

    uint32_t dht_fail_count = 0;  /* consecutive failed reads; error only raised at threshold */
    bool prev_any_error = false;

    const uint32_t DHT_EVERY = DHT_READ_INTERVAL_MS / SENSOR_TASK_PERIOD_MS;
    const uint32_t LOG_EVERY = SENSOR_LOG_INTERVAL_MS / SENSOR_TASK_PERIOD_MS;

    for (;;) {
        esp_task_wdt_reset();

        hw_sensor_data_t local = {0};

        /* ── ADC sensors ─────────────────────────────────────────── */
        local.water_level_raw = adc_read_raw(SENSOR_ADC_WATER_LEVEL);
        local.ldr_raw         = adc_read_raw(SENSOR_ADC_LDR);

        local.err_water_level = (local.water_level_raw < 0);
        local.err_ldr         = (local.ldr_raw         < 0);

        if (local.err_water_level) { local.water_level_raw = 0; }
        if (local.err_ldr)         { local.ldr_raw         = 0; }

        /* ── RoC fault detection for LDR and water level ─────────────
         * A driver error resets the detector so the first good reading
         * after recovery is not compared against a stale last_value.
         * If the RoC check fires, err_* is raised and the value is
         * treated as invalid by all downstream consumers. */
        if (local.err_ldr) {
            roc_reset(&s_roc_ldr);
        } else if (roc_update(&s_roc_ldr, &ROC_LDR_PARAMS, local.ldr_raw)) {
            local.err_ldr = true;
        }

        if (local.err_water_level) {
            roc_reset(&s_roc_water);
        } else if (roc_update(&s_roc_water, &ROC_WATER_PARAMS, local.water_level_raw)) {
            local.err_water_level = true;
        }

        /* ── Digital sensors ─────────────────────────────────────── */
        local.pir_detected   = (gpio_get_level(SENSOR_PIN_PIR)    == 1);
        local.button_pressed = (gpio_get_level(SENSOR_PIN_BUTTON) == 0);

        /* ── Ultrasonic ──────────────────────────────────────────── */
        float dist = us_measure_cm();
        if (dist < 0.0f || !in_range_f(dist, SENSOR_DISTANCE_CM_MIN, SENSOR_DISTANCE_CM_MAX)) {
            local.err_distance = true;
            local.distance_cm  = last_distance;
        } else {
            local.err_distance = false;
            local.distance_cm  = dist;
            last_distance      = dist;
        }

        /* ── DHT (once per second) ───────────────────────────────── */
        if (dht_counter == 0) {
            float temp = 0.0f, hum = 0.0f;
            esp_err_t dht_err = dht_read(&temp, &hum);
            if (dht_err != ESP_OK) {
                /* Retry once: FreeRTOS preemption during bit-bang causes spurious
                 * timeouts; a short yield lets the preempting task finish. */
                vTaskDelay(pdMS_TO_TICKS(20));
                dht_err = dht_read(&temp, &hum);
            }
            if (dht_err == ESP_OK) {
                dht_fail_count = 0;
                if (in_range_f(temp, SENSOR_TEMP_C_MIN, SENSOR_TEMP_C_MAX)) {
                    local.err_temperature = false;
                    local.temperature_c   = temp;
                    last_temp             = temp;
                } else {
                    local.err_temperature = true;
                    local.temperature_c   = last_temp;
                }
                if (in_range_f(hum, SENSOR_HUMIDITY_MIN, SENSOR_HUMIDITY_MAX)) {
                    local.err_humidity = false;
                    local.humidity_pct = hum;
                    last_humidity      = hum;
                } else {
                    local.err_humidity = true;
                    local.humidity_pct = last_humidity;
                }
            } else {
                dht_fail_count++;
                local.temperature_c = last_temp;
                local.humidity_pct  = last_humidity;
                if (dht_fail_count >= DHT_FAIL_THRESHOLD) {
                    local.err_temperature = true;
                    local.err_humidity    = true;
                    if (dht_fail_count == DHT_FAIL_THRESHOLD) {
                        ESP_LOGW(TAG, "DHT persistent fault: %u consecutive failures",
                                 (unsigned)dht_fail_count);
                    }
                }
                /* below threshold: keep last good values, no error flag */
            }
        } else {
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

        /* ── Error state change logging ──────────────────────────── */
        bool any_error = local.err_water_level || local.err_ldr ||
                         local.err_distance ||
                         local.err_temperature || local.err_humidity;
        if (any_error != prev_any_error) {
            if (any_error) {
                ESP_LOGW(TAG, "Sensor fault(s): wlvl=%d ldr=%d dist=%d temp=%d hum=%d",
                         local.err_water_level, local.err_ldr,
                         local.err_distance,
                         local.err_temperature, local.err_humidity);
            } else {
                ESP_LOGI(TAG, "All sensors OK");
            }
            prev_any_error = any_error;
        }

        /* ── Periodic debug log every 500 ms ─────────────────────── */
        if (log_counter == 0) {
            char s_wlvl[8], s_ldr[8], s_dist[10], s_temp[10], s_hum[10];
            if (local.err_water_level) snprintf(s_wlvl, sizeof(s_wlvl), " ERR");
            else                       snprintf(s_wlvl, sizeof(s_wlvl), "%4d",   local.water_level_raw);
            if (local.err_ldr)         snprintf(s_ldr,  sizeof(s_ldr),  " ERR");
            else                       snprintf(s_ldr,  sizeof(s_ldr),  "%4d",   local.ldr_raw);
            if (local.err_distance)    snprintf(s_dist, sizeof(s_dist), "  ERR");
            else                       snprintf(s_dist, sizeof(s_dist), "%5.1f", local.distance_cm);
            if (local.err_temperature) snprintf(s_temp, sizeof(s_temp), "  ERR");
            else                       snprintf(s_temp, sizeof(s_temp), "%5.1f", local.temperature_c);
            if (local.err_humidity)    snprintf(s_hum,  sizeof(s_hum),  "  ERR");
            else                       snprintf(s_hum,  sizeof(s_hum),  "%5.1f", local.humidity_pct);
            ESP_LOGI(TAG,
                     "wlvl=%s ldr=%s dist=%scm temp=%sC hum=%s%% pir=%d btn=%d err=%s",
                     s_wlvl, s_ldr, s_dist, s_temp, s_hum,
                     local.pir_detected, local.button_pressed,
                     any_error ? "YES" : "no");
        }
        log_counter = (log_counter + 1) % LOG_EVERY;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}
