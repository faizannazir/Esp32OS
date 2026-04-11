/**
 * @file os_mqtt_test.c
 * @brief Unit tests for os_mqtt interface behavior
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "os_mqtt.h"
#include "esp_err.h"

#define TEST_PASS(fmt, ...) printf("[PASS] " fmt "\n", ##__VA_ARGS__)
#define TEST_FAIL(fmt, ...) do { \
    printf("[FAIL] " fmt "\n", ##__VA_ARGS__); \
    return false; \
} while (0)

#define TEST_ASSERT(cond, fmt, ...) do { \
    if (!(cond)) TEST_FAIL(fmt, ##__VA_ARGS__); \
} while (0)

typedef bool (*test_func_t)(void);

static int g_tests_passed = 0;
static int g_tests_failed = 0;

static void run_test(const char *name, test_func_t test)
{
    printf("\n>>> Running: %s\n", name);
    if (test()) {
        g_tests_passed++;
        printf("[OK] %s PASSED\n", name);
    } else {
        g_tests_failed++;
        printf("[XX] %s FAILED\n", name);
    }
}

static void mqtt_cleanup(void)
{
    if (os_mqtt_get_state() == OS_MQTT_STATE_CONNECTED ||
        os_mqtt_get_state() == OS_MQTT_STATE_CONNECTING) {
        os_mqtt_disconnect();
    }
    os_mqtt_deinit();
}

static bool test_mqtt_init_idempotent(void)
{
    esp_err_t ret = os_mqtt_init();
    TEST_ASSERT(ret == ESP_OK, "init should succeed");

    ret = os_mqtt_init();
    TEST_ASSERT(ret == ESP_OK, "second init should be idempotent");

    mqtt_cleanup();
    TEST_PASS("MQTT init is idempotent");
    return true;
}

static bool test_mqtt_config_validation(void)
{
    os_mqtt_init();

    esp_err_t ret = os_mqtt_config(NULL);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "NULL config must fail");

    os_mqtt_config_t cfg = {0};
    ret = os_mqtt_config(&cfg);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "empty broker URL must fail");

    strncpy(cfg.broker_url, "mqtt://127.0.0.1:1883", sizeof(cfg.broker_url) - 1);
    ret = os_mqtt_config(&cfg);
    TEST_ASSERT(ret == ESP_OK, "valid config should succeed");

    const char *url = os_mqtt_get_broker_url();
    TEST_ASSERT(url != NULL && strcmp(url, "mqtt://127.0.0.1:1883") == 0,
                "broker URL should be persisted");

    mqtt_cleanup();
    TEST_PASS("MQTT config validation works");
    return true;
}

static bool test_mqtt_publish_when_disconnected(void)
{
    os_mqtt_init();

    os_mqtt_config_t cfg = {0};
    strncpy(cfg.broker_url, "mqtt://127.0.0.1:1883", sizeof(cfg.broker_url) - 1);
    os_mqtt_config(&cfg);

    esp_err_t ret = os_mqtt_publish("test/topic", "hello", 5, OS_MQTT_QOS_0, false);
    TEST_ASSERT(ret == ESP_ERR_INVALID_STATE,
                "publish while disconnected must return ESP_ERR_INVALID_STATE");

    mqtt_cleanup();
    TEST_PASS("MQTT publish state validation works");
    return true;
}

static bool test_mqtt_subscription_validation(void)
{
    os_mqtt_init();

    esp_err_t ret = os_mqtt_subscribe(NULL, OS_MQTT_QOS_0);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "NULL topic subscribe must fail");

    char long_topic[OS_MQTT_TOPIC_MAX_LEN + 4];
    memset(long_topic, 'a', sizeof(long_topic) - 1);
    long_topic[sizeof(long_topic) - 1] = '\0';

    ret = os_mqtt_subscribe(long_topic, OS_MQTT_QOS_1);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "oversized topic must fail");

    ret = os_mqtt_subscribe("sensor/temp", OS_MQTT_QOS_1);
    TEST_ASSERT(ret == ESP_OK, "valid topic subscribe should succeed");

    ret = os_mqtt_unsubscribe("sensor/temp");
    TEST_ASSERT(ret == ESP_OK, "unsubscribe should succeed");

    mqtt_cleanup();
    TEST_PASS("MQTT subscribe/unsubscribe validation works");
    return true;
}

static bool test_mqtt_stats_and_state_strings(void)
{
    os_mqtt_init();

    TEST_ASSERT(strcmp(os_mqtt_get_state_str(OS_MQTT_STATE_DISCONNECTED), "DISCONNECTED") == 0,
                "state string mapping for DISCONNECTED must match");
    TEST_ASSERT(strcmp(os_mqtt_get_state_str(OS_MQTT_STATE_CONNECTED), "CONNECTED") == 0,
                "state string mapping for CONNECTED must match");

    os_mqtt_stats_t stats = {0};
    os_mqtt_get_stats(&stats);
    os_mqtt_clear_stats();
    memset(&stats, 0xA5, sizeof(stats));
    os_mqtt_get_stats(&stats);

    TEST_ASSERT(stats.messages_sent == 0 && stats.messages_received == 0,
                "cleared stats should be zeroed");

    mqtt_cleanup();
    TEST_PASS("MQTT stats APIs work");
    return true;
}

void os_mqtt_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS MQTT TEST SUITE\n");
    printf("========================================\n");

    run_test("MQTT Init Idempotent", test_mqtt_init_idempotent);
    run_test("MQTT Config Validation", test_mqtt_config_validation);
    run_test("MQTT Publish While Disconnected", test_mqtt_publish_when_disconnected);
    run_test("MQTT Subscription Validation", test_mqtt_subscription_validation);
    run_test("MQTT Stats and State Strings", test_mqtt_stats_and_state_strings);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}