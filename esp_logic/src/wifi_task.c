/*
 * @file    wifi_task.c
 * @brief   WiFi station + HTTP server task for esp_logic: UC4 web interface.
 *          Serves GET / (dashboard SPA) and GET /api/data (sensor JSON).
 *          POST /api/fan returns 404 until UC4.4 is implemented; JS handles
 *          this silently.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#include "wifi_task.h"
#include "task_config.h"
#include "comm_task.h"
#include "display_task.h"
#include "control_task.h"
#include "control_logic.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_task";

#define WIFI_CONNECTED_BIT  BIT0

static EventGroupHandle_t s_wifi_event_group;
static httpd_handle_t     s_server = NULL;

/* ── Embedded single-page dashboard ─────────────────────────────────
 * Pure HTML/CSS/JS — no external CDN.  Under 4 KB; safe for a single
 * httpd_resp_send() call without chunked transfer.
 * No double-quotes in HTML/JS so no C-string escaping is needed for
 * the page content itself (only the JSON format string below uses \").
 * ─────────────────────────────────────────────────────────────────── */
static const char HTML_PAGE[] =
    "<!DOCTYPE html><html lang=en><head>"
    "<meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Smart Farm</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:sans-serif;margin:0;padding:16px;background:#eef2ee}"
    "h1{text-align:center;color:#2d622d;margin:0 0 12px}"
    "h2{text-align:center;font-size:.8em;text-transform:uppercase;"
    "color:#888;margin:0 0 6px}"
    ".row{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;"
    "margin-bottom:12px}"
    ".card{background:#fff;border-radius:8px;padding:10px 14px;"
    "min-width:100px;text-align:center;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
    ".v{font-size:1.7em;font-weight:700;color:#2d622d}"
    ".l{font-size:.75em;color:#888}"
    ".badge{padding:6px 16px;border-radius:14px;font-weight:700;color:#fff}"
    ".ok{background:#28a745}.refill{background:#dc3545}"
    ".eco{background:#007bff}.normal{background:#28a745}"
    ".perf{background:#fd7e14}"
    "button{padding:8px 18px;border:2px solid #ccc;border-radius:6px;"
    "font-size:.9em;cursor:pointer;background:#fff;color:#555}"
    "button.active.eco{background:#007bff;border-color:#007bff;color:#fff}"
    "button.active.normal{background:#28a745;border-color:#28a745;color:#fff}"
    "button.active.perf{background:#fd7e14;border-color:#fd7e14;color:#fff}"
    "</style></head><body>"
    "<h1>Smart Farm</h1>"
    "<h2>Sensors</h2>"
    "<div class=row>"
    "<div class=card><div class=v id=temp>--</div>"
    "<div class=l>Temp &#176;C</div></div>"
    "<div class=card><div class=v id=hum>--</div>"
    "<div class=l>Humidity %</div></div>"
    "<div class=card><div class=v id=wlvl>--</div>"
    "<div class=l>Water Level</div></div>"
    "<div class=card><div class=v id=ldr>--</div>"
    "<div class=l>LDR</div></div>"
    "<div class=card><div class=v id=pir>--</div>"
    "<div class=l>PIR</div></div>"
    "</div>"
    "<h2>System State</h2>"
    "<div class=row>"
    "<span class='badge ok' id=wstate>--</span>"
    "<span class='badge normal' id=fmode>--</span>"
    "</div>"
    "<h2>Fan Control</h2>"
    "<div class=row>"
    "<button class=eco data-m=ECO id=bE>ECO</button>"
    "<button class=normal data-m=NORMAL id=bN>NORMAL</button>"
    "<button class=perf data-m=PERFORMANCE id=bP>PERFORMANCE</button>"
    "</div>"
    "<script>"
    "function upd(d){"
    "document.getElementById('temp').textContent=d.temperature.toFixed(1);"
    "document.getElementById('hum').textContent=d.humidity.toFixed(1);"
    "document.getElementById('wlvl').textContent=d.water_level;"
    "document.getElementById('ldr').textContent=d.ldr;"
    "document.getElementById('pir').textContent=d.pir_detected?'YES':'NO';"
    "var w=document.getElementById('wstate');"
    "w.textContent='Water: '+d.water_state;"
    "w.className='badge '+(d.water_state==='OK'?'ok':'refill');"
    "var f=document.getElementById('fmode');"
    "f.textContent='Fan: '+d.fan_mode;"
    "f.className='badge '+(d.fan_mode==='ECO'?'eco':"
    "d.fan_mode==='PERFORMANCE'?'perf':'normal');"
    "['bE','bN','bP'].forEach(function(x){"
    "document.getElementById(x).classList.remove('active')});"
    "var bm={ECO:'bE',NORMAL:'bN',PERFORMANCE:'bP'};"
    "if(bm[d.fan_mode])document.getElementById(bm[d.fan_mode])"
    ".classList.add('active');}"
    "function poll(){"
    "fetch('/api/data').then(function(r){return r.json();})"
    ".then(upd).catch(function(){});}"
    "function fan(m){"
    "fetch('/api/fan',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({mode:m})})"
    ".catch(function(){}).then(poll);}"
    "document.querySelectorAll('button[data-m]').forEach(function(b){"
    "b.addEventListener('click',function(){fan(b.dataset.m);});});"
    "poll();setInterval(poll,2000);"
    "</script></body></html>";

/* ── Format temperature (d10 = degrees C * 10) into "23.5" or "-0.5" */
static void format_temp(char *buf, size_t bufsz, int temp_d10)
{
    if (temp_d10 < 0) {
        int pos = -temp_d10;
        snprintf(buf, bufsz, "-%d.%d", pos / 10, pos % 10);
    } else {
        snprintf(buf, bufsz, "%d.%d", temp_d10 / 10, temp_d10 % 10);
    }
}

/* ── HTTP handler: GET / ──────────────────────────────────────────── */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, sizeof(HTML_PAGE) - 1);
    return ESP_OK;
}

/* ── HTTP handler: GET /api/data ──────────────────────────────────── */
static esp_err_t api_data_handler(httpd_req_t *req)
{
    sensor_data_t snap = {0};
    if (g_rx_data_valid &&
        xSemaphoreTake(g_rx_sensor_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        snap = g_rx_sensor_data;
        xSemaphoreGive(g_rx_sensor_mutex);
    }

    char temp_str[12];
    format_temp(temp_str, sizeof(temp_str), (int)snap.temperature);

    int          h        = (int)snap.humidity;
    unsigned int uptime_s = (unsigned int)(esp_timer_get_time() / 1000000LL);

    char json[280];
    int  len = snprintf(json, sizeof(json),
        "{\"temperature\":%s,\"humidity\":%d.%d,"
        "\"water_level\":%u,\"ldr\":%u,\"pir_detected\":%u,"
        "\"uptime_s\":%u,"
        "\"water_state\":\"%s\",\"fan_mode\":\"%s\"}",
        temp_str,
        h / 10, h % 10,
        (unsigned int)snap.water_level,
        (unsigned int)snap.photoresistor,
        (unsigned int)snap.pir_detected,
        uptime_s,
        g_system_state == SYS_STATE_OK ? "OK" : "REFILL",
        profile_name(g_active_profile));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

/* ── Start the HTTP server and register URI handlers ─────────────── */
static void start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    static const httpd_uri_t uri_root = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = root_handler,
    };
    static const httpd_uri_t uri_api_data = {
        .uri     = "/api/data",
        .method  = HTTP_GET,
        .handler = api_data_handler,
    };

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_api_data);
    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}

/* ── WiFi event handler ───────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            esp_wifi_connect();
            ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected, IP obtained");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ── Public init (call from app_main before scheduler) ───────────── */
void wifi_task_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    configASSERT(s_wifi_event_group != NULL);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid,     WIFI_SSID, sizeof(WIFI_SSID) - 1);
    memcpy(wifi_config.sta.password, WIFI_PASS, sizeof(WIFI_PASS) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "WiFi initialised (SSID=%s)", WIFI_SSID);
}

/* ── Task loop ───────────────────────────────────────────────────── */
void wifi_task(void *arg)
{
    esp_task_wdt_add(NULL);

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Kick watchdog every 1 s while waiting for IP */
    while (!(xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    start_http_server();

    for (;;) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(WIFI_TASK_PERIOD_MS));
    }
}
