/**
 * @file os_mqtt.h
 * @brief MQTT Client Interface for ESP32OS
 *
 * Provides publish/subscribe messaging to MQTT brokers with
 * automatic reconnection and TLS support.
 *
 * Copyright (c) 2026 ESP32OS Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   Configuration Constants
   ──────────────────────────────────────────────── */

/** Maximum MQTT broker URL length */
#define OS_MQTT_URL_MAX_LEN 128

/** Maximum client ID length */
#define OS_MQTT_CLIENT_ID_MAX_LEN 32

/** Maximum topic length */
#define OS_MQTT_TOPIC_MAX_LEN 128

/** Maximum payload size for single message */
#define OS_MQTT_MAX_PAYLOAD_LEN 1024

/** Maximum number of subscriptions */
#define OS_MQTT_MAX_SUBSCRIPTIONS 8

/** Default port for MQTT (non-TLS) */
#define OS_MQTT_DEFAULT_PORT 1883

/** Default port for MQTT over TLS */
#define OS_MQTT_DEFAULT_PORT_TLS 8883

/** Keepalive interval in seconds */
#define OS_MQTT_KEEPALIVE_SEC 60

/** Reconnect delay base in milliseconds */
#define OS_MQTT_RECONNECT_DELAY_MS 5000

/* ────────────────────────────────────────────────
   QoS Levels
   ──────────────────────────────────────────────── */

typedef enum {
    OS_MQTT_QOS_0 = 0,  /**< Fire and forget */
    OS_MQTT_QOS_1 = 1,  /**< At least once delivery */
    OS_MQTT_QOS_2 = 2   /**< Exactly once delivery */
} os_mqtt_qos_t;

/* ────────────────────────────────────────────────
   Connection State
   ──────────────────────────────────────────────── */

typedef enum {
    OS_MQTT_STATE_DISCONNECTED = 0,
    OS_MQTT_STATE_CONNECTING = 1,
    OS_MQTT_STATE_CONNECTED = 2,
    OS_MQTT_STATE_DISCONNECTING = 3,
    OS_MQTT_STATE_ERROR = 4
} os_mqtt_state_t;

/* ────────────────────────────────────────────────
   Event Types
   ──────────────────────────────────────────────── */

typedef enum {
    OS_MQTT_EVENT_CONNECTED = 0,     /**< Connected to broker */
    OS_MQTT_EVENT_DISCONNECTED = 1,  /**< Disconnected from broker */
    OS_MQTT_EVENT_SUBSCRIBED = 2,    /**< Subscription acknowledged */
    OS_MQTT_EVENT_UNSUBSCRIBED = 3,  /**< Unsubscription acknowledged */
    OS_MQTT_EVENT_PUBLISHED = 4,      /**< Message published */
    OS_MQTT_EVENT_DATA = 5,           /**< Message received */
    OS_MQTT_EVENT_ERROR = 6           /**< Error occurred */
} os_mqtt_event_type_t;

/* ────────────────────────────────────────────────
   Event Data Structure
   ──────────────────────────────────────────────── */

typedef struct {
    os_mqtt_event_type_t event_type;

    /* For CONNECTED event */
    int session_present;

    /* For DATA event */
    const char *topic;
    uint16_t topic_len;
    const char *payload;
    uint16_t payload_len;
    os_mqtt_qos_t qos;
    int retain;

    /* For ERROR event */
    esp_err_t error;
} os_mqtt_event_t;

/* ────────────────────────────────────────────────
   Callback Type
   ──────────────────────────────────────────────── */

/**
 * @brief MQTT event callback function type
 *
 * @param event Pointer to event data
 * @param user_data User data pointer passed during registration
 */
typedef void (*os_mqtt_callback_t)(const os_mqtt_event_t *event, void *user_data);

/* ────────────────────────────────────────────────
   Configuration Structure
   ──────────────────────────────────────────────── */

typedef struct {
    char broker_url[OS_MQTT_URL_MAX_LEN];   /**< mqtt://host:port or mqtts://host:port */
    char client_id[OS_MQTT_CLIENT_ID_MAX_LEN]; /**< Client identifier (auto-generated if empty) */

    /* Optional authentication */
    char username[32];
    char password[64];

    /* Optional TLS/SSL */
    const char *ca_cert;          /**< CA certificate in PEM format (NULL for no TLS) */
    const char *client_cert;      /**< Client certificate (NULL if not using mutual TLS) */
    const char *client_key;       /**< Client private key (NULL if not using mutual TLS) */

    /* Connection parameters */
    uint16_t keepalive_sec;       /**< Keepalive interval (0 = use default) */
    bool disable_auto_reconnect;  /**< Set true to disable auto-reconnect */
    uint32_t reconnect_delay_ms;  /**< Initial reconnect delay base */

    /* Callback */
    os_mqtt_callback_t callback;  /**< Event callback function */
    void *user_data;              /**< User data for callback */
} os_mqtt_config_t;

/* ────────────────────────────────────────────────
   Statistics
   ──────────────────────────────────────────────── */

typedef struct {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t messages_dropped;
    uint32_t connect_count;
    uint32_t disconnect_count;
    uint32_t reconnect_attempts;
    uint32_t bytes_sent;
    uint32_t bytes_received;
} os_mqtt_stats_t;

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */

/**
 * @brief Initialize the MQTT subsystem
 *
 * Must be called before any other MQTT functions.
 *
 * @return ESP_OK on success
 */
esp_err_t os_mqtt_init(void);

/**
 * @brief Deinitialize the MQTT subsystem
 *
 * Disconnects from broker and releases resources.
 */
void os_mqtt_deinit(void);

/**
 * @brief Configure MQTT client
 *
 * Sets up broker URL, credentials, and callbacks.
 * Must be called before os_mqtt_connect().
 *
 * @param config Pointer to configuration structure
 *
 * @return ESP_OK on success
 */
esp_err_t os_mqtt_config(const os_mqtt_config_t *config);

/**
 * @brief Connect to MQTT broker
 *
 * Establishes connection using configured parameters.
 * Connection is asynchronous - check state with os_mqtt_get_state().
 *
 * @return ESP_OK if connection initiated
 */
esp_err_t os_mqtt_connect(void);

/**
 * @brief Disconnect from MQTT broker
 *
 * Cleanly disconnects from broker.
 *
 * @return ESP_OK on success
 */
esp_err_t os_mqtt_disconnect(void);

/**
 * @brief Publish a message
 *
 * Publishes payload to specified topic.
 *
 * @param topic   Topic string (null-terminated)
 * @param payload Message payload
 * @param len     Payload length in bytes
 * @param qos     Quality of Service level (0, 1, or 2)
 * @param retain  Set to true for retained message
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t os_mqtt_publish(const char *topic, const char *payload,
                           size_t len, os_mqtt_qos_t qos, bool retain);

/**
 * @brief Subscribe to a topic
 *
 * @param topic Topic filter (e.g., "sensor/+/data")
 * @param qos   Maximum QoS for this subscription
 *
 * @return ESP_OK on success
 */
esp_err_t os_mqtt_subscribe(const char *topic, os_mqtt_qos_t qos);

/**
 * @brief Unsubscribe from a topic
 *
 * @param topic Topic filter to unsubscribe from
 *
 * @return ESP_OK on success
 */
esp_err_t os_mqtt_unsubscribe(const char *topic);

/**
 * @brief Get current connection state
 *
 * @return Current state (DISCONNECTED, CONNECTING, CONNECTED, etc.)
 */
os_mqtt_state_t os_mqtt_get_state(void);

/**
 * @brief Get connection state as string
 *
 * @param state State to convert (use os_mqtt_get_state())
 *
 * @return Human-readable state string
 */
const char *os_mqtt_get_state_str(os_mqtt_state_t state);

/**
 * @brief Check if connected to broker
 *
 * @return true if connected, false otherwise
 */
bool os_mqtt_is_connected(void);

/**
 * @brief Get MQTT statistics
 *
 * @param stats Pointer to statistics structure to fill
 */
void os_mqtt_get_stats(os_mqtt_stats_t *stats);

/**
 * @brief Clear MQTT statistics
 */
void os_mqtt_clear_stats(void);

/**
 * @brief Get broker URL
 *
 * @return Current broker URL or NULL if not configured
 */
const char *os_mqtt_get_broker_url(void);

/**
 * @brief Print MQTT status to file descriptor
 *
 * @param fd File descriptor (-1 for UART)
 */
void os_mqtt_print_status(int fd);

#ifdef __cplusplus
}
#endif
