/*
 * @file    wifi_task.h
 * @brief   WiFi station + HTTP server task for esp_logic: UC4 web interface.
 *          Serves GET / (dashboard HTML) and GET /api/data (sensor JSON).
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-05-01
 */

#ifndef WIFI_TASK_H
#define WIFI_TASK_H

/* WiFi credentials — override at build time via -D flags if needed */
#ifndef WIFI_SSID
#define WIFI_SSID "your_ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your_password"
#endif

_Static_assert(sizeof(WIFI_SSID) - 1 <= 31, "WIFI_SSID exceeds 31 characters");
_Static_assert(sizeof(WIFI_PASS) - 1 <= 63, "WIFI_PASS exceeds 63 characters");

void wifi_task_init(void);
void wifi_task(void *arg);

#endif /* WIFI_TASK_H */
