/*
 * @file    actuator_task.c
 * @brief   Initialises all actuators and applies commands from the queue.
 *          Enforces pump safety rule: max 30 s ON, min 5 s forced OFF.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#include "actuator_task.h"
#include "task_config.h"
#include "sensor_task.h"
#include "lcd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include <stdio.h>

static const char *TAG = "actuator_task";
static bool s_lcd_available = false;

static inline int pump_gpio_level(bool on)
{
    return on ? ACTUATOR_PUMP_ACTIVE_LEVEL : (1 - ACTUATOR_PUMP_ACTIVE_LEVEL);
}

/* ── Shared queue ────────────────────────────────────────────────── */
QueueHandle_t g_actuator_queue;

/* ── Pump safety state ───────────────────────────────────────────── */
typedef struct {
    bool     active;            /* current physical state of the pump */
    bool     forced_off;        /* in mandatory cool-down period        */
    uint32_t on_time_ms;        /* continuous ON duration               */
    uint32_t off_time_ms;       /* continuous OFF duration              */
} pump_safety_t;

/* ── LEDC helpers ────────────────────────────────────────────────── */
static void ledc_init_servo(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_SERVO,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = ACTUATOR_PIN_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_SERVO,
        .timer_sel  = LEDC_TIMER_SERVO,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

static void ledc_init_fan(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_FAN,
        .duty_resolution = FAN_RESOLUTION,
        .freq_hz         = FAN_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    /* io19 (IN+) drives the speed signal */
    ledc_channel_config_t ch = {
        .gpio_num   = ACTUATOR_PIN_FAN_IN_P,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_FAN,
        .timer_sel  = LEDC_TIMER_FAN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

static void gpio_outputs_init(void)
{
    const uint64_t output_mask =
        (1ULL << ACTUATOR_PIN_FAN_IN_N) |
        (1ULL << ACTUATOR_PIN_PUMP)     |
        (1ULL << ACTUATOR_PIN_BUZZER)   |
        (1ULL << ACTUATOR_PIN_LED);

    gpio_config_t cfg = {
        .pin_bit_mask = output_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* Safe initial states: all OFF */
    gpio_set_level(ACTUATOR_PIN_FAN_IN_N, 0);
    gpio_set_level(ACTUATOR_PIN_PUMP,     pump_gpio_level(false));
    gpio_set_level(ACTUATOR_PIN_BUZZER,   0);
    gpio_set_level(ACTUATOR_PIN_LED,      0);
}

/* ── Apply servo angle ──────────────────────────────────────────── */
static void servo_set_angle(uint16_t angle_deg)
{
    if (angle_deg > 180) angle_deg = 180;
    uint32_t duty = SERVO_DUTY_0DEG +
                    ((uint32_t)(SERVO_DUTY_180DEG - SERVO_DUTY_0DEG) * angle_deg) / 180;
    ESP_LOGI(TAG, "servo angle=%u duty=%lu", angle_deg, (unsigned long)duty);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SERVO, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SERVO);
}

static void servo_disable(void)
{
    /* Use set_duty(0)+update instead of ledc_stop() — ledc_stop() hands the
     * pin back to GPIO-idle and ledc_update_duty() won't re-enable it without
     * a full channel re-config on some ESP-IDF versions. */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SERVO, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SERVO);
}

/* ── Apply fan speed ─────────────────────────────────────────────── */
static void fan_set_speed(bool enable, uint8_t duty)
{
    if (!enable || duty == 0) {
        gpio_set_level(ACTUATOR_PIN_FAN_IN_N, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN);
        return;
    }

    /* Single-direction drive: IN- stays LOW, IN+ receives 0..255 PWM duty. */
    gpio_set_level(ACTUATOR_PIN_FAN_IN_N, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN);
}

/* ── Pump safety enforcement ─────────────────────────────────────── */
/*
 * Returns the actual physical pump state after hard safety limiting.
 * Control timing is owned by esp_logic; this layer only prevents runaway ON.
 */
static bool pump_safety_update(pump_safety_t *ps, bool requested, uint32_t dt_ms)
{
    if (ps->forced_off) {
        /* Mandatory cool-down: ignore requests until OFF for PUMP_MIN_OFF_MS */
        ps->off_time_ms += dt_ms;
        ps->on_time_ms   = 0;
        if (ps->off_time_ms >= PUMP_MIN_OFF_MS) {
            ps->forced_off  = false;
            ps->off_time_ms = 0;
            ESP_LOGI(TAG, "Pump cool-down finished – ready");
        }
        return false;
    }

    if (requested) {
        ps->active       = true;
        ps->on_time_ms  += dt_ms;
        ps->off_time_ms  = 0;
        if (ps->on_time_ms >= PUMP_MAX_ON_MS) {
            ps->active      = false;
            ps->forced_off  = true;
            ps->on_time_ms  = 0;
            ps->off_time_ms = 0;
            ESP_LOGW(TAG, "Pump safety: max ON time reached – forcing OFF");
            gpio_set_level(ACTUATOR_PIN_PUMP, pump_gpio_level(false));
            return false;
        }
        return true;
    } else {
        ps->active      = false;
        ps->on_time_ms  = 0;
        ps->off_time_ms += dt_ms;
        return false;
    }
}

/* ── LCD refresh: show content matching display_mode ─────────────── */
static const char *profile_label(uint8_t profile)
{
    switch (profile) {
        case 0:  return "ECO";
        case 2:  return "PERF";
        case 1:
        default: return "NORMAL";
    }
}

static const char *water_label(uint8_t display_mode)
{
    switch (display_mode) {
        case 1:  return "WARNING";
        case 2:  return "CRITICAL";
        case 3:  return "ALARM";
        case 0:
        default: return "OK";
    }
}

static void lcd_refresh(uint8_t display_mode, uint8_t profile)
{
    if (!s_lcd_available) {
        return;
    }

    char line1[LCD_COLS + 1];
    char line2[LCD_COLS + 1];

    snprintf(line1, sizeof(line1), "PROFILE:%-8s", profile_label(profile));
    snprintf(line2, sizeof(line2), "WATER:%-10s",  water_label(display_mode));

    /* No lcd_clear(): relay switching injects noise on the I2C bus; the 2ms
     * blank period from clear + vTaskDelay is when corruption is most likely.
     * Overwriting 16 padded chars per line avoids blanking and is sufficient. */
    lcd_set_cursor(0, 0);
    lcd_write_string(line1);
    lcd_set_cursor(0, 1);
    lcd_write_string(line2);
}

/* ── Public init ─────────────────────────────────────────────────── */
void actuator_task_init(void)
{
    g_actuator_queue = xQueueCreate(ACTUATOR_CMD_QUEUE_SIZE, sizeof(hw_actuator_cmd_t));
    configASSERT(g_actuator_queue != NULL);

    gpio_outputs_init();
    ledc_init_servo();
    ledc_init_fan();

    servo_disable();
    fan_set_speed(false, 0);

    if (lcd_init() == ESP_OK) {
        s_lcd_available = true;
    } else {
        s_lcd_available = false;
        ESP_LOGW(TAG, "LCD init failed – display disabled");
    }
}

/* ── Task loop ───────────────────────────────────────────────────── */
void actuator_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t        last_wake          = xTaskGetTickCount();
    pump_safety_t     ps                 = {0};
    hw_actuator_cmd_t cmd                = {0};
    uint32_t          lcd_counter        = 0;
    uint16_t          servo_target       = 0;
    bool              servo_target_valid = false;
    bool              servo_active       = false;
    TickType_t        servo_move_tick    = 0;

    /* LCD update every 500 ms (50 × 10 ms actuator cycles) */
    const uint32_t LCD_EVERY = 500 / ACTUATOR_TASK_PERIOD_MS;

    for (;;) {
        esp_task_wdt_reset();

        /* Dequeue latest command (non-blocking; keep previous if none) */
        hw_actuator_cmd_t new_cmd;
        if (xQueueReceive(g_actuator_queue, &new_cmd, 0) == pdTRUE) {
            cmd = new_cmd;
        }

        /* ── Fan ─────────────────────────────────────────────────── */
        fan_set_speed(cmd.fan_enable, cmd.fan_duty);

        /* ── Servo ───────────────────────────────────────────────── */
        if (cmd.servo_enable) {
            if (!servo_target_valid || servo_target != cmd.servo_angle_deg) {
                /* New target: apply PWM and start hold timer */
                servo_set_angle(cmd.servo_angle_deg);
                servo_target       = cmd.servo_angle_deg;
                servo_target_valid = true;
                servo_active       = true;
                servo_move_tick    = xTaskGetTickCount();
            } else if (servo_active &&
                       (xTaskGetTickCount() - servo_move_tick) >=
                       pdMS_TO_TICKS(SERVO_HOLD_MS)) {
                /* Servo has had time to reach position – cut PWM */
                servo_disable();
                servo_active = false;
            }
        } else {
            /* Failsafe: cut PWM and forget target */
            servo_disable();
            servo_active       = false;
            servo_target_valid = false;
        }

        /* ── Pump (safety-gated) ─────────────────────────────────── */
        bool pump_on = pump_safety_update(&ps, cmd.pump_enable, ACTUATOR_TASK_PERIOD_MS);
        gpio_set_level(ACTUATOR_PIN_PUMP, pump_gpio_level(pump_on));

        /* ── Buzzer ──────────────────────────────────────────────── */
        gpio_set_level(ACTUATOR_PIN_BUZZER, cmd.buzzer_enable ? 1 : 0);

        /* ── LED ─────────────────────────────────────────────────── */
        gpio_set_level(ACTUATOR_PIN_LED, cmd.led_enable ? 1 : 0);

        /* ── LCD (refresh every 500 ms with latest sensor values) ── */
        if (lcd_counter == 0) {
            lcd_refresh(cmd.display_mode, cmd.profile);
        }
        lcd_counter = (lcd_counter + 1) % LCD_EVERY;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ACTUATOR_TASK_PERIOD_MS));
    }
}
