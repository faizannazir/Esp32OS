#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "os_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

static uint32_t s_callback_count;

static void test_timer_callback(void *arg)
{
    (void)arg;
    s_callback_count++;
}

static bool test_timer_init_idempotent(void)
{
    esp_err_t ret = os_timer_init();
    TEST_ASSERT(ret == ESP_OK, "timer init should succeed");

    ret = os_timer_init();
    TEST_ASSERT(ret == ESP_OK, "second timer init should be idempotent");

    os_timer_deinit();
    TEST_PASS("timer init is idempotent");
    return true;
}

static bool test_timer_create_start_stop_delete(void)
{
    os_timer_init();
    s_callback_count = 0;

    os_timer_config_t cfg = {
        .name = "tm_basic",
        .period_ms = 10,
        .reload = false,
        .callback = test_timer_callback,
        .arg = NULL,
    };

    os_timer_t timer = os_timer_create(&cfg);
    TEST_ASSERT(timer != NULL, "timer creation should succeed");
    TEST_ASSERT(!os_timer_is_active(timer), "timer should start inactive");

    esp_err_t ret = os_timer_start(timer);
    TEST_ASSERT(ret == ESP_OK, "timer start should succeed");
    TEST_ASSERT(os_timer_is_active(timer), "timer should be active after start");

    vTaskDelay(pdMS_TO_TICKS(40));
    TEST_ASSERT(s_callback_count >= 1, "timer callback should fire at least once");

    ret = os_timer_stop(timer);
    TEST_ASSERT(ret == ESP_OK, "timer stop should succeed");

    ret = os_timer_delete(timer);
    TEST_ASSERT(ret == ESP_OK, "timer delete should succeed");

    os_timer_deinit();
    TEST_PASS("timer create/start/stop/delete works");
    return true;
}

static bool test_timer_restart_and_lookup(void)
{
    os_timer_init();

    os_timer_config_t cfg = {
        .name = "tm_restart",
        .period_ms = 50,
        .reload = true,
        .callback = test_timer_callback,
        .arg = NULL,
    };

    os_timer_t timer = os_timer_create(&cfg);
    TEST_ASSERT(timer != NULL, "timer creation should succeed");
    TEST_ASSERT(os_timer_find("tm_restart") == timer, "lookup should find timer by name");

    esp_err_t ret = os_timer_restart(timer, 20);
    TEST_ASSERT(ret == ESP_OK, "restart should succeed");
    TEST_ASSERT(os_timer_is_active(timer), "timer should be active after restart");
    TEST_ASSERT(os_timer_get_name(timer) != NULL, "timer name should be accessible");
    TEST_ASSERT(os_timer_get_fire_count(timer) == 0, "fire count should start at 0");

    ret = os_timer_delete(timer);
    TEST_ASSERT(ret == ESP_OK, "timer delete should succeed");

    os_timer_deinit();
    TEST_PASS("timer restart and lookup work");
    return true;
}

void os_timer_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS TIMER TEST SUITE\n");
    printf("========================================\n");

    run_test("Timer Init Idempotent", test_timer_init_idempotent);
    run_test("Timer Create/Start/Stop/Delete", test_timer_create_start_stop_delete);
    run_test("Timer Restart and Lookup", test_timer_restart_and_lookup);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}