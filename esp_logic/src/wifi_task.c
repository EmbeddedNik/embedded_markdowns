/*
 * @file    wifi_task.c
 * @brief   WiFi Access Point and HTTP server task for esp_logic (UC4.1).
 *          Starts AP mode with SSID "SmartFarm-ESP32", then launches an
 *          HTTP server with stub handlers for /, /api/data, and /api/fan.
 *          Does not share state with any other task.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#include "wifi_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_http_server.h"

#include <string.h>

static const char *TAG = "wifi_task";

/* ── AP configuration ────────────────────────────────────────────── */
#define WIFI_AP_SSID        "SmartFarm-ESP32"
#define WIFI_AP_PASSWORD    "smartfarm123"
#define WIFI_AP_MAX_CONN    4

/* ── HTTP handler: GET / ─────────────────────────────────────────── */
static esp_err_t handler_root_get(httpd_req_t *req)
{
    httpd_resp_send(req, "SmartFarm UI", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ── HTTP handler: GET /api/data ─────────────────────────────────── */
static esp_err_t handler_api_data_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ── HTTP handler: POST /api/fan ─────────────────────────────────── */
static esp_err_t handler_api_fan_post(httpd_req_t *req)
{
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── URI descriptor table ────────────────────────────────────────── */
static const httpd_uri_t uri_root = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = handler_root_get,
    .user_ctx = NULL,
};

static const httpd_uri_t uri_api_data = {
    .uri      = "/api/data",
    .method   = HTTP_GET,
    .handler  = handler_api_data_get,
    .user_ctx = NULL,
};

static const httpd_uri_t uri_api_fan = {
    .uri      = "/api/fan",
    .method   = HTTP_POST,
    .handler  = handler_api_fan_post,
    .user_ctx = NULL,
};

/* ── Internal helpers ────────────────────────────────────────────── */
static void wifi_ap_start(void)
{
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return;
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid           = WIFI_AP_SSID,
            .ssid_len       = (uint8_t)strlen(WIFI_AP_SSID),
            .password       = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID=%s max_conn=%d", WIFI_AP_SSID, WIFI_AP_MAX_CONN);
}

static void http_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_api_data);
    httpd_register_uri_handler(server, &uri_api_fan);

    ESP_LOGI(TAG, "HTTP server started");
}

/* ── Public API ──────────────────────────────────────────────────── */
void wifi_task_init(void)
{
    /* Nothing to pre-initialise outside the task */
}

void wifi_task(void *arg)
{
    (void)arg;

    wifi_ap_start();
    http_server_start();

    /* WiFi and HTTP server run autonomously; keep task alive */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
