#include "os_networking.h"
#include "os_logging.h"

#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/icmp.h"
#include "lwip/ip.h"
#include "lwip/raw.h"
#include "lwip/timeouts.h"
#include "esp_timer.h"
#include "esp_http_client.h"

#define TAG "NET"

/* ────────────────────────────────────────────────
   WiFi Event Group Bits
   ──────────────────────────────────────────────── */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define WIFI_MAX_RETRY           5

/* ────────────────────────────────────────────────
   State
   ──────────────────────────────────────────────── */
static struct {
    bool                initialised;
    esp_netif_t        *netif_sta;
    EventGroupHandle_t  events;
    int                 retry_count;
    os_net_status_t     status;
} s_net = {
    .initialised = false,
    .netif_sta   = NULL,
    .events      = NULL,
    .retry_count = 0,
};

/* ────────────────────────────────────────────────
   Event Handlers
   ──────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *ev = data;
            OS_LOGW(TAG, "WiFi disconnected (reason=%d)", ev->reason);
            memset(&s_net.status, 0, sizeof(s_net.status));
            if (s_net.retry_count < WIFI_MAX_RETRY) {
                s_net.retry_count++;
                esp_wifi_connect();
                OS_LOGI(TAG, "Reconnect attempt %d/%d", s_net.retry_count, WIFI_MAX_RETRY);
            } else {
                xEventGroupSetBits(s_net.events, WIFI_FAIL_BIT);
            }
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            OS_LOGD(TAG, "Scan done");
            xEventGroupSetBits(s_net.events, BIT2);
            break;
        default:
            break;
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        snprintf(s_net.status.ip,      sizeof(s_net.status.ip),
                 IPSTR, IP2STR(&ev->ip_info.ip));
        snprintf(s_net.status.gw,      sizeof(s_net.status.gw),
                 IPSTR, IP2STR(&ev->ip_info.gw));
        snprintf(s_net.status.netmask, sizeof(s_net.status.netmask),
                 IPSTR, IP2STR(&ev->ip_info.netmask));
        s_net.status.connected = true;
        s_net.retry_count = 0;

        /* Read MAC */
        esp_wifi_get_mac(WIFI_IF_STA, s_net.status.mac);

        OS_LOGI(TAG, "Got IP: %s  GW: %s", s_net.status.ip, s_net.status.gw);
        xEventGroupSetBits(s_net.events, WIFI_CONNECTED_BIT);
    }
}

/* ────────────────────────────────────────────────
   Init
   ──────────────────────────────────────────────── */
esp_err_t os_net_init(void)
{
    if (s_net.initialised) return ESP_OK;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_net.netif_sta = esp_netif_create_default_wifi_sta();
    if (!s_net.netif_sta) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               ip_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_net.events = xEventGroupCreate();
    s_net.initialised = true;

    OS_LOGI(TAG, "Network stack initialised");

    /* Try auto-connect from NVS */
    char ssid[33] = {0}, pass[65] = {0};
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t ssid_sz = sizeof(ssid), pass_sz = sizeof(pass);
        if (nvs_get_str(h, "ssid", ssid, &ssid_sz) == ESP_OK &&
            nvs_get_str(h, "pass", pass, &pass_sz) == ESP_OK &&
            strlen(ssid) > 0) {
            OS_LOGI(TAG, "Auto-connecting to '%s' from NVS", ssid);
            os_wifi_connect(ssid, pass);
        }
        nvs_close(h);
    }

    return ESP_OK;
}

/* ────────────────────────────────────────────────
   Scan
   ──────────────────────────────────────────────── */
int os_wifi_scan(os_wifi_scan_result_t *results, int max_results)
{
    if (!s_net.initialised || !results) return -1;

    wifi_scan_config_t scan_cfg = {
        .ssid       = NULL,
        .bssid      = NULL,
        .channel    = 0,
        .show_hidden= true,
        .scan_type  = WIFI_SCAN_TYPE_ACTIVE,
    };

    xEventGroupClearBits(s_net.events, BIT2);
    if (esp_wifi_scan_start(&scan_cfg, false) != ESP_OK) return -1;

    /* Wait up to 5s for scan done */
    EventBits_t bits = xEventGroupWaitBits(s_net.events, BIT2, pdTRUE,
                                            pdFALSE, pdMS_TO_TICKS(5000));
    if (!(bits & BIT2)) return 0;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return 0;

    uint16_t fetch = (ap_count < (uint16_t)max_results) ? ap_count : (uint16_t)max_results;
    wifi_ap_record_t *ap_list = calloc(fetch, sizeof(wifi_ap_record_t));
    if (!ap_list) return 0;

    esp_wifi_scan_get_ap_records(&fetch, ap_list);

    for (int i = 0; i < fetch; i++) {
        strncpy(results[i].ssid, (char *)ap_list[i].ssid, 32);
        memcpy(results[i].bssid, ap_list[i].bssid, 6);
        results[i].rssi    = ap_list[i].rssi;
        results[i].channel = ap_list[i].primary;
        results[i].open    = (ap_list[i].authmode == WIFI_AUTH_OPEN);
    }
    free(ap_list);
    return (int)fetch;
}

/* ────────────────────────────────────────────────
   Connect
   ──────────────────────────────────────────────── */
esp_err_t os_wifi_connect(const char *ssid, const char *password)
{
    if (!s_net.initialised) return ESP_ERR_INVALID_STATE;
    if (!ssid) return ESP_ERR_INVALID_ARG;

    s_net.retry_count = 0;
    xEventGroupClearBits(s_net.events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_cfg.sta.password, password,
                sizeof(wifi_cfg.sta.password) - 1);
    }
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if (!password || strlen(password) == 0) {
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    /* Store SSID for status reporting */
    strncpy(s_net.status.ssid, ssid, sizeof(s_net.status.ssid) - 1);

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_net.events,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    OS_LOGW(TAG, "Failed to connect to '%s'", ssid);
    return ESP_FAIL;
}

void os_wifi_disconnect(void)
{
    esp_wifi_disconnect();
    memset(&s_net.status, 0, sizeof(s_net.status));
}

void os_wifi_get_status(os_net_status_t *st)
{
    if (!st) return;
    *st = s_net.status;
    if (st->connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            st->rssi = ap.rssi;
        }
    }
}

esp_err_t os_wifi_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open("wifi_cfg", NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    nvs_set_str(h, "ssid", ssid ? ssid : "");
    nvs_set_str(h, "pass", password ? password : "");
    nvs_commit(h);
    nvs_close(h);
    OS_LOGI(TAG, "WiFi credentials saved for '%s'", ssid);
    return ESP_OK;
}

/* ────────────────────────────────────────────────
   Ping  (ICMP using raw socket)
   ──────────────────────────────────────────────── */
#define PING_DATA_SIZE  32
#define PING_TIMEOUT_MS 1000

static uint16_t ping_checksum(const void *buf, size_t len)
{
    const uint16_t *p = buf;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(uint8_t *)p;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)~sum;
}

esp_err_t os_ping(const char *host, int count, os_ping_result_t *result)
{
    if (!host || !result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    result->min_ms = UINT32_MAX;

    /* Resolve host */
    struct hostent *he = gethostbyname(host);
    if (!he) return ESP_FAIL;

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    memcpy(&dest.sin_addr, he->h_addr, he->h_length);

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) return ESP_FAIL;

    esp_err_t ret = ESP_OK;
    struct timeval tv = { .tv_sec = PING_TIMEOUT_MS / 1000,
                          .tv_usec = (PING_TIMEOUT_MS % 1000) * 1000 };
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    uint8_t packet[sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE];
    struct icmp_echo_hdr *icmp_hdr = (struct icmp_echo_hdr *)packet;

    for (int i = 0; i < count; i++) {
        memset(packet, 0, sizeof(packet));
        icmp_hdr->type    = ICMP_ECHO;
        icmp_hdr->code    = 0;
        icmp_hdr->id      = htons(0xBEEF);
        icmp_hdr->seqno   = htons((uint16_t)i);
        /* Fill data */
        for (int j = 0; j < PING_DATA_SIZE; j++)
            packet[sizeof(struct icmp_echo_hdr) + j] = (uint8_t)(j & 0xFF);
        icmp_hdr->chksum  = 0;
        icmp_hdr->chksum  = ping_checksum(packet, sizeof(packet));

        int64_t t_start = esp_timer_get_time();
        result->sent++;

        if (sendto(sock, packet, sizeof(packet), 0,
                   (struct sockaddr *)&dest, sizeof(dest)) < 0) continue;

        uint8_t recv_buf[64];
        socklen_t slen = sizeof(dest);
        int recv_len = recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                                (struct sockaddr *)&dest, &slen);
        if (recv_len < 0) continue;

        int64_t rtt_us = esp_timer_get_time() - t_start;
        uint32_t rtt_ms = (uint32_t)(rtt_us / 1000);
        result->received++;

        if (rtt_ms < result->min_ms) result->min_ms = rtt_ms;
        if (rtt_ms > result->max_ms) result->max_ms = rtt_ms;
        result->avg_ms = (result->avg_ms * (result->received - 1) + rtt_ms)
                         / result->received;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

cleanup:
    close(sock);
    if (result->min_ms == UINT32_MAX) result->min_ms = 0;
    return ret;
}

/* ────────────────────────────────────────────────
   HTTP GET
   ──────────────────────────────────────────────── */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (!evt->user_data) return ESP_OK;
        char *buf = evt->user_data;
        /* buf is (data_buf | size_remaining[4]) packed structure trick:
           We'll use a simpler approach with output_len tracked in user_data */
    }
    return ESP_OK;
}

int os_http_get(const char *url, char *buf, size_t buf_sz)
{
    if (!url || !buf || buf_sz == 0) return -1;
    buf[0] = '\0';

    esp_http_client_config_t config = {
        .url            = url,
        .method         = HTTP_METHOD_GET,
        .timeout_ms     = 5000,
        .event_handler  = http_event_handler,
        .user_data      = buf,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;

    int len = -1;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        len = esp_http_client_read_response(client, buf, (int)buf_sz - 1);
        if (len >= 0) buf[len] = '\0';
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return len;
}
