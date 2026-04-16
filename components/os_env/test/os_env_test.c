#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "os_env.h"
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

static bool test_env_init_idempotent(void)
{
    esp_err_t ret = os_env_init();
    TEST_ASSERT(ret == ESP_OK, "env init should succeed");

    ret = os_env_init();
    TEST_ASSERT(ret == ESP_OK, "second env init should be idempotent");

    os_env_clear();
    os_env_deinit();
    TEST_PASS("env init is idempotent");
    return true;
}

static bool test_env_set_get_expand(void)
{
    os_env_init();

    esp_err_t ret = os_env_set("NAME", "Esp32OS");
    TEST_ASSERT(ret == ESP_OK, "set should succeed");

    char value[64] = {0};
    ret = os_env_get("NAME", value, sizeof(value));
    TEST_ASSERT(ret == ESP_OK, "get should succeed");
    TEST_ASSERT(strcmp(value, "Esp32OS") == 0, "value should match");

    char expanded[64] = {0};
    size_t len = os_env_expand("hello $NAME", expanded, sizeof(expanded));
    TEST_ASSERT(len > 0, "expand should produce output");
    TEST_ASSERT(strcmp(expanded, "hello Esp32OS") == 0, "expanded text should match");

    os_env_clear();
    os_env_deinit();
    TEST_PASS("env set/get/expand works");
    return true;
}

static bool test_env_unset_and_clear(void)
{
    os_env_init();

    esp_err_t ret = os_env_set("TMP", "1");
    TEST_ASSERT(ret == ESP_OK, "set should succeed");

    ret = os_env_unset("TMP");
    TEST_ASSERT(ret == ESP_OK, "unset should succeed");

    char value[8] = {0};
    ret = os_env_get("TMP", value, sizeof(value));
    TEST_ASSERT(ret == ESP_ERR_NOT_FOUND, "unset value should be removed");

    ret = os_env_set("A", "1");
    TEST_ASSERT(ret == ESP_OK, "set A should succeed");
    ret = os_env_clear();
    TEST_ASSERT(ret == ESP_OK, "clear should succeed");
    TEST_ASSERT(os_env_get("A", value, sizeof(value)) == ESP_ERR_NOT_FOUND,
                "clear should remove values");

    os_env_deinit();
    TEST_PASS("env unset/clear works");
    return true;
}

void os_env_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS ENV TEST SUITE\n");
    printf("========================================\n");

    run_test("Env Init Idempotent", test_env_init_idempotent);
    run_test("Env Set/Get/Expand", test_env_set_get_expand);
    run_test("Env Unset/Clear", test_env_unset_and_clear);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}
