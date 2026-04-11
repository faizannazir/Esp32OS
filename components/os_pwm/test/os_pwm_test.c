/**
 * @file os_pwm_test.c
 * @brief Unit tests for os_pwm interface behavior
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "os_pwm.h"
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

static bool test_pwm_init_idempotent(void)
{
    esp_err_t ret = os_pwm_init();
    TEST_ASSERT(ret == ESP_OK, "PWM init should succeed");

    ret = os_pwm_init();
    TEST_ASSERT(ret == ESP_OK, "second init should be idempotent");

    os_pwm_deinit();
    TEST_PASS("PWM init is idempotent");
    return true;
}

static bool test_pwm_invalid_arguments(void)
{
    os_pwm_init();

    esp_err_t ret = os_pwm_channel_init(OS_PWM_CHANNEL_INVALID, 2, 1000);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "invalid channel should fail");

    ret = os_pwm_channel_init(0, 255, 1000);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "invalid GPIO should fail");

    ret = os_pwm_channel_init(0, 2, 0);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "zero frequency should fail");

    os_pwm_deinit();
    TEST_PASS("PWM invalid argument checks pass");
    return true;
}

static bool test_pwm_channel_lifecycle(void)
{
    os_pwm_init();

    esp_err_t ret = os_pwm_channel_init(0, 2, 1000);
    TEST_ASSERT(ret == ESP_OK, "channel init should succeed");
    TEST_ASSERT(os_pwm_channel_is_active(0), "channel must be active after init");
    TEST_ASSERT(os_pwm_get_active_count() >= 1, "active count should be >= 1");

    uint8_t gpio = 0;
    uint32_t freq = 0;
    uint8_t duty = 0;
    ret = os_pwm_get_config(0, &gpio, &freq, &duty);
    TEST_ASSERT(ret == ESP_OK, "get_config should succeed for active channel");
    TEST_ASSERT(gpio == 2, "GPIO should match initialized pin");
    TEST_ASSERT(freq == 1000, "frequency should match initialized value");
    TEST_ASSERT(duty == 0, "initial duty should be 0");

    ret = os_pwm_set_duty(0, 50);
    TEST_ASSERT(ret == ESP_OK, "set duty percent should succeed");

    ret = os_pwm_set_duty_us(0, 500);
    TEST_ASSERT(ret == ESP_OK, "set duty microseconds should succeed");

    ret = os_pwm_set_freq(0, 2000);
    TEST_ASSERT(ret == ESP_OK, "set frequency should succeed");

    ret = os_pwm_channel_deinit(0);
    TEST_ASSERT(ret == ESP_OK, "channel deinit should succeed");
    TEST_ASSERT(!os_pwm_channel_is_active(0), "channel must be inactive after deinit");

    os_pwm_deinit();
    TEST_PASS("PWM channel lifecycle works");
    return true;
}

static bool test_pwm_state_checks(void)
{
    os_pwm_deinit();

    esp_err_t ret = os_pwm_set_duty(0, 10);
    TEST_ASSERT(ret == ESP_ERR_INVALID_STATE,
                "set duty before init should fail with invalid state");

    ret = os_pwm_get_config(0, NULL, NULL, NULL);
    TEST_ASSERT(ret != ESP_OK,
                "get_config before channel init should fail");

    TEST_PASS("PWM state checks work");
    return true;
}

void os_pwm_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS PWM TEST SUITE\n");
    printf("========================================\n");

    run_test("PWM Init Idempotent", test_pwm_init_idempotent);
    run_test("PWM Invalid Arguments", test_pwm_invalid_arguments);
    run_test("PWM Channel Lifecycle", test_pwm_channel_lifecycle);
    run_test("PWM State Checks", test_pwm_state_checks);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}