/*
 * @file    wifi_task.c
 * @brief   UC4 WiFi SoftAP and HTTP dashboard for esp_logic.
 *          Serves GET /, GET /api/data and POST /api/fan from the ESP32 AP.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#include "wifi_task.h"
#include "comm_task.h"
#include "control_logic.h"
#include "control_task.h"
#include "display_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_task";

#define FAN_BODY_BUF_SIZE 64

static httpd_handle_t s_server = NULL;

static const char HTML_PAGE[] =
    "<!DOCTYPE html><html lang=en><head>"
    "<meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>SmartFarm</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:Arial,sans-serif;margin:0;padding:16px;background:#edf3ef;color:#1f2a24}"
    "h1{text-align:center;margin:2px 0 4px;color:#1f5f3a}"
    ".ip{text-align:center;color:#5d6d63;font-size:.9em;margin:0 0 14px}"
    "h2{text-align:center;font-size:.78em;text-transform:uppercase;color:#65756b;margin:16px 0 8px}"
    ".row{display:grid;grid-template-columns:repeat(auto-fit,minmax(115px,1fr));gap:8px;max-width:760px;margin:0 auto}"
    ".card{background:#fff;border:1px solid #dbe4de;border-radius:8px;padding:12px;text-align:center}"
    ".v{font-size:1.55em;font-weight:700;color:#1f5f3a;min-height:1.3em}"
    ".l{font-size:.75em;color:#65756b;margin-top:4px}"
    ".badge{display:inline-block;padding:8px 14px;border-radius:8px;font-weight:700;color:#fff;min-width:112px;text-align:center}"
    ".ok{background:#238b45}.refill{background:#c0392b}.eco{background:#1f77b4}.normal{background:#238b45}.perf{background:#d66a00}.stale{background:#687076}"
    "button{border:2px solid #c7d2cc;background:#fff;border-radius:8px;padding:10px 12px;font-weight:700;color:#26332b}"
    "button.active.eco{background:#1f77b4;border-color:#1f77b4;color:#fff}"
    "button.active.normal{background:#238b45;border-color:#238b45;color:#fff}"
    "button.active.perf{background:#d66a00;border-color:#d66a00;color:#fff}"
    "</style></head><body>"
    "<h1>SmartFarm</h1><p class=ip>Verbunden mit SmartFarm-ESP32 - http://192.168.4.1/</p>"
    "<h2>Sensorik</h2><div class=row>"
    "<div class=card><div class=v id=temp>--</div><div class=l>Temperatur C</div></div>"
    "<div class=card><div class=v id=hum>--</div><div class=l>Luftfeuchte %</div></div>"
    "<div class=card><div class=v id=water>--</div><div class=l>Fuellstand</div></div>"
    "<div class=card><div class=v id=ldr>--</div><div class=l>LDR</div></div>"
    "<div class=card><div class=v id=pir>--</div><div class=l>PIR</div></div>"
    "</div><h2>Zustaende</h2><div class=row>"
    "<div class=card><span class='badge stale' id=data>--</span><div class=l>Daten</div></div>"
    "<div class=card><span class='badge stale' id=wstate>--</span><div class=l>Wasser</div></div>"
    "<div class=card><span class='badge stale' id=fmode>--</span><div class=l>Luefterprofil</div></div>"
    "</div><h2>Luefterprofil</h2><div class=row>"
    "<button class=eco data-mode=ECO id=bE>ECO</button>"
    "<button class=normal data-mode=NORMAL id=bN>NORMAL</button>"
    "<button class=perf data-mode=PERFORMANCE id=bP>PERFORMANCE</button>"
    "</div><script>"
    "function txt(id,v){document.getElementById(id).textContent=v;}"
    "function cls(el,c){el.className='badge '+c;}"
    "function upd(d){"
    "txt('temp',d.temperature);txt('hum',d.humidity);txt('water',d.water_level);txt('ldr',d.ldr);txt('pir',d.pir_detected?'YES':'NO');"
    "var data=document.getElementById('data');txt('data',d.data_valid?'OK':'STALE');cls(data,d.data_valid?'ok':'stale');"
    "var w=document.getElementById('wstate');txt('wstate',d.water_state);cls(w,d.water_state==='OK'?'ok':'refill');"
    "var f=document.getElementById('fmode');txt('fmode',d.fan_mode);cls(f,d.fan_mode==='ECO'?'eco':d.fan_mode==='PERFORMANCE'?'perf':'normal');"
    "['bE','bN','bP'].forEach(function(x){document.getElementById(x).classList.remove('active')});"
    "var bm={ECO:'bE',NORMAL:'bN',PERFORMANCE:'bP'};if(bm[d.fan_mode])document.getElementById(bm[d.fan_mode]).classList.add('active');"
    "}"
    "function poll(){fetch('/api/data').then(function(r){return r.json()}).then(upd).catch(function(){})}"
    "function fan(m){fetch('/api/fan',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:m})}).then(poll).catch(function(){})}"
    "document.querySelectorAll('button[data-mode]').forEach(function(b){b.addEventListener('click',function(){fan(b.dataset.mode)})});"
    "poll();setInterval(poll,2000);"
    "</script></body></html>";

static void format_d10(char *buf, size_t bufsz, int value_d10)
{
    int whole = value_d10 / 10;
    int frac = value_d10 % 10;
    if (frac < 0) {
        frac = -frac;
    }
    snprintf(buf, bufsz, "%d.%d", whole, frac);
}

static const char *water_state_name(system_state_t state)
{
    return state == SYS_STATE_REFILL ? "REFILL" : "OK";
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_data_get_handler(httpd_req_t *req)
{
    sensor_data_t snap = {0};
    bool data_valid = false;

    if (g_rx_data_valid &&
        xSemaphoreTake(g_rx_sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        snap = g_rx_sensor_data;
        xSemaphoreGive(g_rx_sensor_mutex);
        data_valid = true;
    }

    char temp[16];
    char hum[16];
    format_d10(temp, sizeof(temp), snap.temperature);
    format_d10(hum, sizeof(hum), snap.humidity);

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    uint32_t age_ms = data_valid ? (uint32_t)(now_ms - g_rx_sensor_timestamp_ms) : 0u;

    system_profile_t profile = g_active_profile;
    system_state_t water_state = g_system_state;

    char json[384];
    int len = snprintf(json, sizeof(json),
                       "{"
                       "\"data_valid\":%s,"
                       "\"sensor_age_ms\":%u,"
                       "\"temperature\":\"%s\","
                       "\"humidity\":\"%s\","
                       "\"water_level\":%u,"
                       "\"ldr\":%u,"
                       "\"pir_detected\":%u,"
                       "\"water_state\":\"%s\","
                       "\"fan_mode\":\"%s\""
                       "}",
                       data_valid ? "true" : "false",
                       (unsigned)age_ms,
                       temp,
                       hum,
                       (unsigned)snap.water_level,
                       (unsigned)snap.photoresistor,
                       (unsigned)snap.pir_detected,
                       water_state_name(water_state),
                       profile_name(profile));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

static bool parse_profile(const char *body, system_profile_t *profile)
{
    if (strstr(body, "PERFORMANCE") != NULL) {
        *profile = SYSTEM_PROFILE_PERFORMANCE;
        return true;
    }
    if (strstr(body, "NORMAL") != NULL) {
        *profile = SYSTEM_PROFILE_NORMAL;
        return true;
    }
    if (strstr(body, "ECO") != NULL) {
        *profile = SYSTEM_PROFILE_ECO;
        return true;
    }
    return false;
}

static esp_err_t api_fan_post_handler(httpd_req_t *req)
{
    char body[FAN_BODY_BUF_SIZE];
    int recv_len = req->content_len;

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

    system_profile_t profile;
    if (!parse_profile(body, &profile)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"result\":\"error\"}");
        return ESP_OK;
    }

    g_active_profile = profile;
    ESP_LOGI(TAG, "Profile changed via web UI: %s", profile_name(profile));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
    return ESP_OK;
}

static bool init_wifi_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase before WiFi init: %s", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool start_softap(void)
{
    if (!init_wifi_nvs()) {
        return false;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = (uint8_t)strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    if (strlen(WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "SoftAP started: SSID=%s password=%s URL=http://192.168.4.1/",
             WIFI_AP_SSID, WIFI_AP_PASSWORD);
    return true;
}

static void start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 3;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    static const httpd_uri_t api_data = {
        .uri = "/api/data",
        .method = HTTP_GET,
        .handler = api_data_get_handler,
    };
    static const httpd_uri_t api_fan = {
        .uri = "/api/fan",
        .method = HTTP_POST,
        .handler = api_fan_post_handler,
    };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &api_data);
    httpd_register_uri_handler(s_server, &api_fan);

    ESP_LOGI(TAG, "HTTP server started: GET /, GET /api/data, POST /api/fan");
}

void wifi_task_init(void)
{
    /* WiFi is started inside wifi_task after the scheduler is running. */
}

void wifi_task(void *arg)
{
    (void)arg;

    esp_task_wdt_add(NULL);
    if (start_softap()) {
        start_http_server();
    } else {
        ESP_LOGE(TAG, "WiFi SoftAP startup failed; keeping other tasks alive");
    }

    for (;;) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
