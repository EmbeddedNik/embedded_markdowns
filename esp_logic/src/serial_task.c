/*
 * @file    serial_task.c
 * @brief   UART0 USB command handler: e=eco, n=normal, p=performance.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#include "serial_task.h"
#include "control_task.h"
#include "task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "serial_task";

#define SERIAL_UART_NUM       UART_NUM_0
#define SERIAL_BAUD_RATE      115200
#define SERIAL_RX_BUF_SIZE    256

static const char *profile_name(system_profile_t profile)
{
    switch (profile) {
        case SYSTEM_PROFILE_ECO:         return "ECO";
        case SYSTEM_PROFILE_NORMAL:      return "NORMAL";
        case SYSTEM_PROFILE_PERFORMANCE: return "PERFORMANCE";
        default:                         return "UNKNOWN";
    }
}

static void serial_write_text(const char *text)
{
    uart_write_bytes(SERIAL_UART_NUM, text, (uint32_t)strlen(text));
}

void serial_task_init(void)
{
    uart_config_t cfg = {
        .baud_rate = SERIAL_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_param_config(SERIAL_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(SERIAL_UART_NUM, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    esp_err_t ret = uart_driver_install(SERIAL_UART_NUM, SERIAL_RX_BUF_SIZE,
                                        0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    ESP_LOGI(TAG, "UART0 profile input ready: e=eco, n=normal, p=performance");
}

void serial_task(void *arg)
{
    esp_task_wdt_add(NULL);

    const char prompt[] = "\r\nProfile commands: e=eco, n=normal, p=performance\r\n";
    uart_write_bytes(SERIAL_UART_NUM, prompt, sizeof(prompt) - 1u);

    for (;;) {
        esp_task_wdt_reset();

        uint8_t byte = 0;
        int len = uart_read_bytes(SERIAL_UART_NUM, &byte, 1,
                                  pdMS_TO_TICKS(SERIAL_TASK_PERIOD_MS));
        if (len > 0) {
            system_profile_t new_profile = g_active_profile;
            bool valid = true;

            switch (byte) {
                case 'e':
                case 'E':
                    new_profile = SYSTEM_PROFILE_ECO;
                    break;
                case 'n':
                case 'N':
                    new_profile = SYSTEM_PROFILE_NORMAL;
                    break;
                case 'p':
                case 'P':
                    new_profile = SYSTEM_PROFILE_PERFORMANCE;
                    break;
                case '\r':
                case '\n':
                    valid = false;
                    break;
                default:
                    valid = false;
                    serial_write_text("\r\nUse e, n, or p.\r\n");
                    break;
            }

            if (valid && new_profile != g_active_profile) {
                g_active_profile = new_profile;
                ESP_LOGI(TAG, "Profile selected via USB serial: %s",
                         profile_name(new_profile));
                serial_write_text("\r\nProfile: ");
                serial_write_text(profile_name(new_profile));
                serial_write_text("\r\n");
            } else if (valid) {
                serial_write_text("\r\nProfile unchanged: ");
                serial_write_text(profile_name(new_profile));
                serial_write_text("\r\n");
            }
        }
    }
}
