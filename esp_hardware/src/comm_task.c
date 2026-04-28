/*
 * @file    comm_task.c
 * @brief   UART2 communication stub: initialises the bus, loop is empty.
 *          Full protocol (sensor TX / actuator RX) is implemented in UC1.3.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-28
 */

#include "comm_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "comm_task";

/* ── Public init ─────────────────────────────────────────────────── */
void comm_task_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = COMM_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(COMM_UART_NUM, &cfg);
    uart_set_pin(COMM_UART_NUM, COMM_PIN_TX, COMM_PIN_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(COMM_UART_NUM,
                        256 /* rx buf */, 0 /* tx buf (use TX FIFO) */,
                        0, NULL, 0);

    ESP_LOGI(TAG, "UART2 initialised (TX=io%d RX=io%d baud=%d) – stub only",
             COMM_PIN_TX, COMM_PIN_RX, COMM_BAUD_RATE);
}

/* ── Task loop (stub) ────────────────────────────────────────────── */
void comm_task(void *arg)
{
    esp_task_wdt_add(NULL);

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        esp_task_wdt_reset();

        /* UC1.3 will add: read sensor_data → serialise → UART2 TX
         *                  read UART2 RX → deserialise → actuator_queue */

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMM_TASK_PERIOD_MS));
    }
}
