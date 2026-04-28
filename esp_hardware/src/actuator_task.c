/*
 * @file    actuator_task.c
 * @brief   Initialises all actuators and applies commands from the queue.
 *          Enforces pump safety rule: max 30 s ON, min 5 s forced OFF.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#include "actuator_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "actuator_task";

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
        .duty       = SERVO_DUTY_0DEG,
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
    gpio_set_level(ACTUATOR_PIN_PUMP,     0);
    gpio_set_level(ACTUATOR_PIN_BUZZER,   0);
    gpio_set_level(ACTUATOR_PIN_LED,      0);
}

/* ── Apply servo angle ──────────────────────────────────────────── */
static void servo_set_angle(uint16_t angle_deg)
{
    if (angle_deg > 180) angle_deg = 180;
    uint32_t duty = SERVO_DUTY_0DEG +
                    ((uint32_t)(SERVO_DUTY_180DEG - SERVO_DUTY_0DEG) * angle_deg) / 180;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SERVO, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SERVO);
}

/* ── Apply fan speed ─────────────────────────────────────────────── */
static void fan_set_speed(bool enable, uint8_t speed_pct)
{
    if (!enable || speed_pct == 0) {
        /* Stop: IN- LOW, IN+ LOW */
        gpio_set_level(ACTUATOR_PIN_FAN_IN_N, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN);
        return;
    }
    if (speed_pct > 100) speed_pct = 100;
    /* Forward direction: IN- LOW, IN+ PWM */
    gpio_set_level(ACTUATOR_PIN_FAN_IN_N, 0);
    uint32_t duty = ((uint32_t)speed_pct * 255U) / 100U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FAN);
}

/* ── Pump safety enforcement ─────────────────────────────────────── */
/*
 * Returns the actual physical state the pump should be in after applying
 * safety rules.  Updates *ps timing counters.
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
        ps->on_time_ms  += dt_ms;
        ps->off_time_ms  = 0;
        if (ps->on_time_ms >= PUMP_MAX_ON_MS) {
            ps->forced_off = true;
            ps->on_time_ms = 0;
            ESP_LOGW(TAG, "Pump safety: max ON time reached – forcing OFF");
            gpio_set_level(ACTUATOR_PIN_PUMP, 0);
            return false;
        }
        return true;
    } else {
        ps->off_time_ms += dt_ms;
        ps->on_time_ms   = 0;
        return false;
    }
}

/* ── Public init ─────────────────────────────────────────────────── */
void actuator_task_init(void)
{
    g_actuator_queue = xQueueCreate(ACTUATOR_CMD_QUEUE_SIZE, sizeof(actuator_cmd_t));
    configASSERT(g_actuator_queue != NULL);

    gpio_outputs_init();
    ledc_init_servo();
    ledc_init_fan();

    servo_set_angle(0);
    fan_set_speed(false, 0);
}

/* ── Task loop ───────────────────────────────────────────────────── */
void actuator_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t    last_wake = xTaskGetTickCount();
    pump_safety_t ps        = {0};
    actuator_cmd_t cmd      = {0};

    for (;;) {
        esp_task_wdt_reset();

        /* Dequeue latest command (non-blocking; keep previous if none) */
        actuator_cmd_t new_cmd;
        if (xQueueReceive(g_actuator_queue, &new_cmd, 0) == pdTRUE) {
            cmd = new_cmd;
        }

        /* ── Fan ─────────────────────────────────────────────────── */
        fan_set_speed(cmd.fan_enable, cmd.fan_speed_pct);

        /* ── Servo ───────────────────────────────────────────────── */
        servo_set_angle(cmd.servo_angle_deg);

        /* ── Pump (safety-gated) ─────────────────────────────────── */
        bool pump_on = pump_safety_update(&ps, cmd.pump_enable, ACTUATOR_TASK_PERIOD_MS);
        gpio_set_level(ACTUATOR_PIN_PUMP, pump_on ? 1 : 0);

        /* ── Buzzer ──────────────────────────────────────────────── */
        gpio_set_level(ACTUATOR_PIN_BUZZER, cmd.buzzer_enable ? 1 : 0);

        /* ── LED ─────────────────────────────────────────────────── */
        gpio_set_level(ACTUATOR_PIN_LED, cmd.led_enable ? 1 : 0);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ACTUATOR_TASK_PERIOD_MS));
    }
}
