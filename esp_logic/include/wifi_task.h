/*
 * @file    wifi_task.h
 * @brief   WiFi SoftAP and HTTP dashboard task for esp_logic UC4.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#ifndef WIFI_TASK_H
#define WIFI_TASK_H

#define WIFI_AP_SSID        "SmartFarm-ESP32"
#define WIFI_AP_PASSWORD    "smartfarm123"
#define WIFI_AP_MAX_CONN    4

void wifi_task_init(void);
void wifi_task(void *arg);

#endif /* WIFI_TASK_H */
