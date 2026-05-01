/*
 * @file    wifi_task.c
 * @brief   WiFi and HTTP server task for esp_logic: UC4.2–UC4.4 web control.
 *          UC4.4: POST /api/fan — changes the active system profile via HTTP.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#include "wifi_task.h"
#include "control_task.h"
#include "control_logic.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "wifi_task";

#define FAN_BODY_BUF_SIZE   64

/* ── POST /api/fan ───────────────────────────────────────────────── */

static esp_err_t fan_post_handler(httpd_req_t *req)
{
    char body[FAN_BODY_BUF_SIZE];
    int  recv_len = req->content_len;

    if (recv_len <= 0 || recv_len >= (int)sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"result\":\"error\"}");
        return ESP_OK;
    }

    int n = httpd_req_recv(req, body, (size_t)recv_len);
    if (n <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"result\":\"error\"}");
        return ESP_OK;
    }
    body[n] = '\0';

    system_profile_t new_profile;
    /* Check PERFORMANCE before NORMAL — longest match first */
    if (strstr(body, "\"PERFORMANCE\"")) {
        new_profile = SYSTEM_PROFILE_PERFORMANCE;
    } else if (strstr(body, "\"NORMAL\"")) {
        new_profile = SYSTEM_PROFILE_NORMAL;
    } else if (strstr(body, "\"ECO\"")) {
        new_profile = SYSTEM_PROFILE_ECO;
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"result\":\"error\"}");
        return ESP_OK;
    }

    g_active_profile = new_profile;
    ESP_LOGI(TAG, "Profile changed via HTTP: %s", profile_name(new_profile));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
    return ESP_OK;
}

static const httpd_uri_t fan_post_uri = {
    .uri     = "/api/fan",
    .method  = HTTP_POST,
    .handler = fan_post_handler,
};

/* ── HTTP server startup ─────────────────────────────────────────── */

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    httpd_register_uri_handler(server, &fan_post_uri);
    ESP_LOGI(TAG, "HTTP server started — POST /api/fan registered");
    return server;
}

/* ── Public API ──────────────────────────────────────────────────── */

void wifi_task_init(void)
{
    start_webserver();
}

void wifi_task(void *arg)
{
    esp_task_wdt_add(NULL);
    for (;;) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
