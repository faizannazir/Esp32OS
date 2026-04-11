/**
 * @file os_mqtt.c
 * @brief MQTT Client Implementation using ESP-IDF MQTT
 *
 * Copyright (c) 2026 ESP32OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "os_mqtt.h"
#include "os_logging.h"
#include "os_networking.h"

#include "mqtt_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

/* ────────────────────────────────────────────────
   Module Configuration
   ──────────────────────────────────────────────── */

#define TAG "OS_MQTT"

/* ────────────────────────────────────────────────
   Module State
   ──────────────────────────────────────────────── */

/** MQTT client handle */
static esp_mqtt_client_handle_t s_client = NULL;

/** Current configuration */
static os_mqtt_config_t s_config = {0};

/** Current state */
static os_mqtt_state_t s_state = OS_MQTT_STATE_DISCONNECTED;

/** Statistics */
static os_mqtt_stats_t s_stats = {0};

/** Mutex protecting state and config */
static SemaphoreHandle_t s_mutex = NULL;

/** Module initialized flag */
static bool s_initialized = false;

/** Auto-reconnect flag (from config) */
static bool s_auto_reconnect = true;

/** Reconnect delay in ms */
static uint32_t s_reconnect_delay_ms = OS_MQTT_RECONNECT_DELAY_MS;

/* ────────────────────────────────────────────────
   Subscription Tracking
   ──────────────────────────────────────────────── */

typedef struct {
    char topic[OS_MQTT_TOPIC_MAX_LEN];
    os_mqtt_qos_t qos;
    bool active;
} subscription_t;

static subscription_t s_subscriptions[OS_MQTT_MAX_SUBSCRIPTIONS];

/* ────────────────────────────────────────────────
   Private Helper Functions
   ──────────────────────────────────────────────── */

/**
 * @brief Convert OS MQTT QoS to ESP-IDF QoS
 */
static int convert_qos(os_mqtt_qos_t qos)
{
    switch (qos) {
        case OS_MQTT_QOS_0: return 0;
        case OS_MQTT_QOS_1: return 1;
        case OS_MQTT_QOS_2: return 2;
        default: return 0;
    }
}

/**
 * @brief Find subscription by topic
 */
static int find_subscription(const char *topic)
{
    if (topic == NULL) return -1;

    for (int i = 0; i < OS_MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].active &&
            strcmp(s_subscriptions[i].topic, topic) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Find free subscription slot
 */
static int find_free_subscription_slot(void)
{
    for (int i = 0; i < OS_MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (!s_subscriptions[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Resubscribe to all topics after reconnect
 */
static void resubscribe_all(void)
{
    for (int i = 0; i < OS_MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].active) {
            esp_mqtt_client_subscribe(s_client, s_subscriptions[i].topic,
                                        convert_qos(s_subscriptions[i].qos));
            OS_LOGI(TAG, "Resubscribed to: %s", s_subscriptions[i].topic);
        }
    }
}

/**
 * @brief Handle MQTT events from ESP-IDF client
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    os_mqtt_event_t os_event = {0};

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_state = OS_MQTT_STATE_CONNECTED;
            s_stats.connect_count++;
            os_event.event_type = OS_MQTT_EVENT_CONNECTED;
            os_event.session_present = event->session_present;
            OS_LOGI(TAG, "Connected to broker, session_present=%d", event->session_present);
            resubscribe_all();
            break;

        case MQTT_EVENT_DISCONNECTED:
            if (s_state == OS_MQTT_STATE_CONNECTED) {
                s_stats.disconnect_count++;
            }
            s_state = OS_MQTT_STATE_DISCONNECTED;
            os_event.event_type = OS_MQTT_EVENT_DISCONNECTED;
            OS_LOGI(TAG, "Disconnected from broker");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            os_event.event_type = OS_MQTT_EVENT_SUBSCRIBED;
            OS_LOGD(TAG, "Subscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            os_event.event_type = OS_MQTT_EVENT_UNSUBSCRIBED;
            OS_LOGD(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            os_event.event_type = OS_MQTT_EVENT_PUBLISHED;
            OS_LOGD(TAG, "Published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            os_event.event_type = OS_MQTT_EVENT_DATA;
            os_event.topic = event->topic;
            os_event.topic_len = event->topic_len;
            os_event.payload = event->data;
            os_event.payload_len = event->data_len;
            os_event.qos = (event->qos == 2) ? OS_MQTT_QOS_2 :
                           (event->qos == 1) ? OS_MQTT_QOS_1 : OS_MQTT_QOS_0;
            os_event.retain = event->retain;

            s_stats.messages_received++;
            s_stats.bytes_received += event->data_len;

            OS_LOGD(TAG, "Received data: topic=%.*s, len=%d",
                    event->topic_len, event->topic, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            s_state = OS_MQTT_STATE_ERROR;
            os_event.event_type = OS_MQTT_EVENT_ERROR;
            os_event.error = ESP_FAIL;
            OS_LOGE(TAG, "MQTT error occurred");
            break;

        default:
            break;
    }

    /* Call user callback if registered */
    if (s_config.callback != NULL && os_event.event_type != 0) {
        /* Release mutex before calling callback to prevent deadlock */
        xSemaphoreGive(s_mutex);
        s_config.callback(&os_event, s_config.user_data);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }

    xSemaphoreGive(s_mutex);
}

/* ────────────────────────────────────────────────
   Public API Implementation
   ──────────────────────────────────────────────── */

esp_err_t os_mqtt_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        OS_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_config, 0, sizeof(s_config));
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_subscriptions, 0, sizeof(s_subscriptions));

    s_state = OS_MQTT_STATE_DISCONNECTED;
    s_auto_reconnect = true;
    s_reconnect_delay_ms = OS_MQTT_RECONNECT_DELAY_MS;

    s_initialized = true;

    OS_LOGI(TAG, "MQTT initialized");
    return ESP_OK;
}

void os_mqtt_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    os_mqtt_disconnect();

    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
    OS_LOGI(TAG, "MQTT deinitialized");
}

esp_err_t os_mqtt_config(const os_mqtt_config_t *config)
{
    if (!s_initialized || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->broker_url[0] == '\0') {
        OS_LOGE(TAG, "Broker URL required");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    memcpy(&s_config, config, sizeof(os_mqtt_config_t));

    s_auto_reconnect = !config->disable_auto_reconnect;
    if (config->reconnect_delay_ms > 0) {
        s_reconnect_delay_ms = config->reconnect_delay_ms;
    }

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "MQTT configured: %s", config->broker_url);
    return ESP_OK;
}

esp_err_t os_mqtt_connect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_config.broker_url[0] == '\0') {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state == OS_MQTT_STATE_CONNECTING || s_state == OS_MQTT_STATE_CONNECTED) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    s_state = OS_MQTT_STATE_CONNECTING;

    /* Build client ID if not provided */
    char client_id[OS_MQTT_CLIENT_ID_MAX_LEN];
    if (s_config.client_id[0] == '\0') {
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(client_id, sizeof(client_id), "esp32os_%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
    } else {
        strncpy(client_id, s_config.client_id, sizeof(client_id) - 1);
        client_id[sizeof(client_id) - 1] = '\0';
    }

    /* Configure MQTT client */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = s_config.broker_url,
        },
        .credentials = {
            .client_id = client_id,
        },
        .session = {
            .keepalive = s_config.keepalive_sec ? s_config.keepalive_sec : OS_MQTT_KEEPALIVE_SEC,
            .disable_clean_session = false,
        },
    };

    /* Add authentication if provided */
    if (s_config.username[0] != '\0') {
        mqtt_cfg.credentials.username = s_config.username;
        if (s_config.password[0] != '\0') {
            mqtt_cfg.credentials.authentication.password = s_config.password;
        }
    }

    /* Add TLS configuration if provided */
    if (s_config.ca_cert != NULL) {
        mqtt_cfg.broker.verification.certificate = s_config.ca_cert;
        mqtt_cfg.broker.verification.skip_cert_common_name_check = false;
    }

    if (s_config.client_cert != NULL && s_config.client_key != NULL) {
        mqtt_cfg.credentials.authentication.certificate = s_config.client_cert;
        mqtt_cfg.credentials.authentication.key = s_config.client_key;
    }

    /* Disable auto reconnect if requested - we handle it ourselves */
    mqtt_cfg.network.disable_auto_reconnect = true;

    xSemaphoreGive(s_mutex);

    /* Create and start client */
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        OS_LOGE(TAG, "Failed to create MQTT client");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_state = OS_MQTT_STATE_ERROR;
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    /* Register event handler */
    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                      mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return ret;
    }

    /* Start connection */
    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return ret;
    }

    s_stats.reconnect_attempts++;

    OS_LOGI(TAG, "Connecting to %s as %s", s_config.broker_url, client_id);
    return ESP_OK;
}

esp_err_t os_mqtt_disconnect(void)
{
    if (!s_initialized || s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = OS_MQTT_STATE_DISCONNECTING;
    xSemaphoreGive(s_mutex);

    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = OS_MQTT_STATE_DISCONNECTED;
    memset(s_subscriptions, 0, sizeof(s_subscriptions));
    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Disconnected from broker");
    return ESP_OK;
}

esp_err_t os_mqtt_publish(const char *topic, const char *payload,
                           size_t len, os_mqtt_qos_t qos, bool retain)
{
    if (!s_initialized || topic == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!os_mqtt_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, (int)len,
                                          convert_qos(qos), retain ? 1 : 0);
    if (msg_id < 0) {
        OS_LOGE(TAG, "Publish failed");
        s_stats.messages_dropped++;
        return ESP_FAIL;
    }

    s_stats.messages_sent++;
    s_stats.bytes_sent += len;

    OS_LOGD(TAG, "Published to %s (msg_id=%d)", topic, msg_id);
    return ESP_OK;
}

esp_err_t os_mqtt_subscribe(const char *topic, os_mqtt_qos_t qos)
{
    if (!s_initialized || topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(topic) >= OS_MQTT_TOPIC_MAX_LEN) {
        OS_LOGE(TAG, "Topic too long");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Check if already subscribed */
    if (find_subscription(topic) >= 0) {
        xSemaphoreGive(s_mutex);
        OS_LOGW(TAG, "Already subscribed to: %s", topic);
        return ESP_OK;
    }

    /* Find free slot */
    int slot = find_free_subscription_slot();
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        OS_LOGE(TAG, "Max subscriptions reached");
        return ESP_ERR_NO_MEM;
    }

    /* Store subscription */
    strncpy(s_subscriptions[slot].topic, topic, OS_MQTT_TOPIC_MAX_LEN - 1);
    s_subscriptions[slot].topic[OS_MQTT_TOPIC_MAX_LEN - 1] = '\0';
    s_subscriptions[slot].qos = qos;
    s_subscriptions[slot].active = true;

    xSemaphoreGive(s_mutex);

    /* Actually subscribe if connected */
    if (os_mqtt_is_connected()) {
        int msg_id = esp_mqtt_client_subscribe(s_client, topic, convert_qos(qos));
        if (msg_id < 0) {
            OS_LOGE(TAG, "Subscribe failed");
            return ESP_FAIL;
        }
        OS_LOGI(TAG, "Subscribed to: %s (msg_id=%d)", topic, msg_id);
    }

    return ESP_OK;
}

esp_err_t os_mqtt_unsubscribe(const char *topic)
{
    if (!s_initialized || topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int slot = find_subscription(topic);
    if (slot >= 0) {
        s_subscriptions[slot].active = false;
    }

    xSemaphoreGive(s_mutex);

    if (os_mqtt_is_connected()) {
        int msg_id = esp_mqtt_client_unsubscribe(s_client, topic);
        if (msg_id < 0) {
            return ESP_FAIL;
        }
    }

    OS_LOGI(TAG, "Unsubscribed from: %s", topic);
    return ESP_OK;
}

os_mqtt_state_t os_mqtt_get_state(void)
{
    if (!s_initialized) {
        return OS_MQTT_STATE_DISCONNECTED;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    os_mqtt_state_t state = s_state;
    xSemaphoreGive(s_mutex);

    return state;
}

const char *os_mqtt_get_state_str(os_mqtt_state_t state)
{
    switch (state) {
        case OS_MQTT_STATE_DISCONNECTED:    return "DISCONNECTED";
        case OS_MQTT_STATE_CONNECTING:      return "CONNECTING";
        case OS_MQTT_STATE_CONNECTED:       return "CONNECTED";
        case OS_MQTT_STATE_DISCONNECTING:   return "DISCONNECTING";
        case OS_MQTT_STATE_ERROR:           return "ERROR";
        default:                            return "UNKNOWN";
    }
}

bool os_mqtt_is_connected(void)
{
    return (os_mqtt_get_state() == OS_MQTT_STATE_CONNECTED);
}

void os_mqtt_get_stats(os_mqtt_stats_t *stats)
{
    if (stats == NULL || !s_initialized) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(stats, &s_stats, sizeof(os_mqtt_stats_t));
    xSemaphoreGive(s_mutex);
}

void os_mqtt_clear_stats(void)
{
    if (!s_initialized) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(&s_stats, 0, sizeof(s_stats));
    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Statistics cleared");
}

const char *os_mqtt_get_broker_url(void)
{
    if (!s_initialized) {
        return NULL;
    }

    return s_config.broker_url;
}

void os_mqtt_print_status(int fd)
{
    (void)fd;

    if (!s_initialized) {
        return;
    }

    os_mqtt_state_t state = os_mqtt_get_state();
    os_mqtt_stats_t stats;
    os_mqtt_get_stats(&stats);

    OS_LOGI(TAG, "State: %s", os_mqtt_get_state_str(state));
    OS_LOGI(TAG, "Messages: sent=%lu, received=%lu, dropped=%lu",
            stats.messages_sent, stats.messages_received, stats.messages_dropped);
    OS_LOGI(TAG, "Bytes: sent=%lu, received=%lu", stats.bytes_sent, stats.bytes_received);
    OS_LOGI(TAG, "Connections: %lu, Disconnects: %lu, Reconnects: %lu",
            stats.connect_count, stats.disconnect_count, stats.reconnect_attempts);
}
