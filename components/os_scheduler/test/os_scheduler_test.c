#include <stdio.h>
#include <stdbool.h>

#include "os_scheduler.h"
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

static bool test_scheduler_init_idempotent(void)
{
    esp_err_t ret = os_scheduler_init();
    TEST_ASSERT(ret == ESP_OK, "scheduler init should succeed");

    ret = os_scheduler_init();
    TEST_ASSERT(ret == ESP_OK, "second init should be idempotent");

    os_scheduler_deinit();
    TEST_PASS("scheduler init is idempotent");
    return true;
}

static bool test_scheduler_background_and_jobs(void)
{
    os_scheduler_init();

    esp_err_t ret = os_scheduler_run_background("run_echo", "echo scheduled", -1);
    TEST_ASSERT(ret == ESP_OK, "background run should succeed");

    ret = os_scheduler_schedule("job_once", "echo later", 10, false, -1);
    TEST_ASSERT(ret == ESP_OK, "one-shot job should schedule");
    TEST_ASSERT(os_scheduler_is_running("job_once"), "job should appear running");

    ret = os_scheduler_cancel("job_once");
    TEST_ASSERT(ret == ESP_OK, "cancel should succeed");
    TEST_ASSERT(!os_scheduler_is_running("job_once"), "cancelled job should stop running");

    os_scheduler_deinit();
    TEST_PASS("scheduler background and job management work");
    return true;
}

void os_scheduler_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS SCHEDULER TEST SUITE\n");
    printf("========================================\n");

    run_test("Scheduler Init Idempotent", test_scheduler_init_idempotent);
    run_test("Scheduler Background and Jobs", test_scheduler_background_and_jobs);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}
