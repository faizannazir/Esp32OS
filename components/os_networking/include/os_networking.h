#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   WiFi Scan Result
   ──────────────────────────────────────────────── */
typedef struct {
    char     ssid[33];
    uint8_t  bssid[6];
    int8_t   rssi;
    uint8_t  channel;
    bool     open;
} os_wifi_scan_result_t;

/* ────────────────────────────────────────────────
   Network Status
   ──────────────────────────────────────────────── */
typedef struct {
    bool     connected;
    char     ssid[33];
    char     ip[16];
    char     gw[16];
    char     netmask[16];
    int8_t   rssi;
    uint8_t  mac[6];
} os_net_status_t;

/* ────────────────────────────────────────────────
   Ping Result
   ──────────────────────────────────────────────── */
typedef struct {
    uint32_t sent;
    uint32_t received;
    uint32_t min_ms;
    uint32_t max_ms;
    uint32_t avg_ms;
} os_ping_result_t;

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */

/** Initialize TCP/IP stack and WiFi driver (call once at startup) */
esp_err_t os_net_init(void);

/** Scan for WiFi networks.  Returns count found (up to max_results). */
int        os_wifi_scan(os_wifi_scan_result_t *results, int max_results);

/** Connect to WiFi AP.  Blocks until connected or timeout. */
esp_err_t  os_wifi_connect(const char *ssid, const char *password);

/** Disconnect from current AP */
void       os_wifi_disconnect(void);

/** Get current connection status */
void       os_wifi_get_status(os_net_status_t *st);

/** Save WiFi credentials to NVS for auto-reconnect */
esp_err_t  os_wifi_save_credentials(const char *ssid, const char *password);

/** Ping a hostname or IP address */
esp_err_t  os_ping(const char *host, int count, os_ping_result_t *result);

/** Simple HTTP GET.  Returns bytes received or -1 on error. */
int        os_http_get(const char *url, char *buf, size_t buf_sz);

#ifdef __cplusplus
}
#endif
