/*
 * @file    wifi_task.c
 * @brief   WiFi HTTP server task for esp_logic: GET /api/data JSON endpoint (UC4.2).
 *          WiFi station connection (UC5.1) must be established before this task starts.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#include "wifi_task.h"

#include "comm_task.h"      /* g_rx_sensor_data, g_rx_sensor_mutex, g_rx_data_valid */
#include "control_task.h"   /* g_active_profile, system_profile_t */
#include "display_task.h"   /* g_system_state, system_state_t */
#include "control_logic.h"  /* profile_name() */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "wifi_task";

static httpd_handle_t s_server = NULL;

/* ── GET /api/data handler ───────────────────────────────────────── */

static esp_err_t api_data_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (!g_rx_data_valid) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"error\":\"no_data\"}");
        return ESP_OK;
    }

    /* Snapshot under mutex; release before snprintf */
    sensor_data_t snap;
    if (xSemaphoreTake(g_rx_sensor_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"error\":\"no_data\"}");
        return ESP_OK;
    }
    snap = g_rx_sensor_data;
    xSemaphoreGive(g_rx_sensor_mutex);

    /* Volatile reads — single 32-bit aligned access is atomic on ESP32 */
    system_profile_t profile = g_active_profile;
    system_state_t   state   = g_system_state;

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{"
             "\"temperature\":%.1f,"
             "\"humidity\":%.1f,"
             "\"water_level\":%u,"
             "\"ldr\":%u,"
             "\"pir_detected\":%u,"
             "\"fan_mode\":\"%s\","
             "\"water_state\":\"%s\""
             "}",
             (float)snap.temperature / 10.0f,
             (float)snap.humidity    / 10.0f,
             (unsigned)snap.water_level,
             (unsigned)snap.photoresistor,
             (unsigned)snap.pir_detected,
             profile_name(profile),
             (state == SYS_STATE_REFILL) ? "REFILL" : "OK");

    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ── HTTP server lifecycle ────────────────────────────────────────── */

static void start_httpd(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static const httpd_uri_t api_data = {
        .uri     = "/api/data",
        .method  = HTTP_GET,
        .handler = api_data_get_handler,
    };
    httpd_register_uri_handler(s_server, &api_data);

    ESP_LOGI(TAG, "HTTP server started, GET /api/data registered");
}

/* ── Public API ──────────────────────────────────────────────────── */

void wifi_task_init(void)
{
}

void wifi_task(void *arg)
{
    (void)arg;
    start_httpd();
    vTaskDelete(NULL);
}
