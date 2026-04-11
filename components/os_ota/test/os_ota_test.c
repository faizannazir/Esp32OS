/**
 * @file os_ota_test.c
 * @brief Unit tests for os_ota interface behavior
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "os_ota.h"
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

static void test_progress_cb(os_ota_state_t state, uint8_t progress,
                             size_t bytes_recv, size_t total_size, void *user_data)
{
    (void)state;
    (void)progress;
    (void)bytes_recv;
    (void)total_size;
    (void)user_data;
}

static bool test_ota_init_idempotent(void)
{
    esp_err_t ret = os_ota_init();
    TEST_ASSERT(ret == ESP_OK, "OTA init should succeed");

    ret = os_ota_init();
    TEST_ASSERT(ret == ESP_OK, "second OTA init should succeed");

    os_ota_deinit();
    TEST_PASS("OTA init is idempotent");
    return true;
}

static bool test_ota_state_and_progress_defaults(void)
{
    os_ota_init();

    TEST_ASSERT(os_ota_get_state() == OS_OTA_STATE_IDLE,
                "initial state should be IDLE");
    TEST_ASSERT(os_ota_get_progress() == 0,
                "initial progress should be 0");
    TEST_ASSERT(!os_ota_is_in_progress(),
                "OTA should not be in progress at startup");

    os_ota_deinit();
    TEST_PASS("OTA default state/progress checks pass");
    return true;
}

static bool test_ota_start_validation(void)
{
    os_ota_init();

    esp_err_t ret = os_ota_start(NULL);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "NULL config must fail");

    os_ota_config_t cfg = {0};
    ret = os_ota_start(&cfg);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "NULL URL must fail");

    cfg.url = "";
    ret = os_ota_start(&cfg);
    TEST_ASSERT(ret != ESP_OK, "empty URL should fail");

    os_ota_deinit();
    TEST_PASS("OTA start input validation works");
    return true;
}

static bool test_ota_info_and_helpers(void)
{
    os_ota_init();

    os_ota_info_t info = {0};
    os_ota_get_info(&info);

    TEST_ASSERT(info.current_version[0] != '\0',
                "current version/partition label should be populated");
    TEST_ASSERT(strcmp(os_ota_get_state_str(OS_OTA_STATE_IDLE), "IDLE") == 0,
                "state string mapping should match");
    TEST_ASSERT(os_ota_get_running_partition() != NULL,
                "running partition string should not be NULL");

    (void)os_ota_can_rollback();
    (void)os_ota_needs_confirmation();

    os_ota_set_progress_callback(test_progress_cb, NULL);
    os_ota_abort();

    os_ota_deinit();
    TEST_PASS("OTA info and helper APIs work");
    return true;
}

void os_ota_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS OTA TEST SUITE\n");
    printf("========================================\n");

    run_test("OTA Init Idempotent", test_ota_init_idempotent);
    run_test("OTA State and Progress Defaults", test_ota_state_and_progress_defaults);
    run_test("OTA Start Validation", test_ota_start_validation);
    run_test("OTA Info and Helpers", test_ota_info_and_helpers);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}