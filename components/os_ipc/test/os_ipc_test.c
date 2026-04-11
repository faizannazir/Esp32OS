/**
 * @file os_ipc_test.c
 * @brief Unit tests for os_ipc queue/event/shm features
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "os_ipc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

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

static bool test_ipc_init_idempotent(void)
{
    esp_err_t ret = os_ipc_init();
    TEST_ASSERT(ret == ESP_OK, "IPC init should succeed");

    ret = os_ipc_init();
    TEST_ASSERT(ret == ESP_OK, "second IPC init should succeed");

    os_ipc_deinit();
    TEST_PASS("IPC init is idempotent");
    return true;
}

static bool test_msgq_send_receive(void)
{
    os_ipc_init();

    os_msgq_t q = os_msgq_create("q_basic", sizeof(uint32_t), 2);
    TEST_ASSERT(q != NULL, "queue creation should succeed");

    uint32_t tx = 0x12345678;
    esp_err_t ret = os_msgq_send(q, &tx, 0);
    TEST_ASSERT(ret == ESP_OK, "queue send should succeed");
    TEST_ASSERT(os_msgq_count(q) == 1, "queue count should be 1");

    uint32_t rx = 0;
    ret = os_msgq_receive(q, &rx, 0);
    TEST_ASSERT(ret == ESP_OK, "queue receive should succeed");
    TEST_ASSERT(rx == tx, "received message should match sent message");

    os_msgq_delete(q);
    os_ipc_deinit();
    TEST_PASS("Message queue send/receive works");
    return true;
}

static bool test_msgq_duplicate_and_limits(void)
{
    os_ipc_init();

    os_msgq_t q1 = os_msgq_create("dup_q", sizeof(uint8_t), 1);
    TEST_ASSERT(q1 != NULL, "first queue creation should succeed");

    os_msgq_t q2 = os_msgq_create("dup_q", sizeof(uint8_t), 1);
    TEST_ASSERT(q2 == NULL, "duplicate queue name must fail");

    uint8_t v = 7;
    os_msgq_send(q1, &v, 0);
    TEST_ASSERT(os_msgq_is_full(q1), "single-depth queue should be full after one send");
    TEST_ASSERT(os_msgq_spaces_available(q1) == 0, "no space should remain when full");

    uint8_t out = 0;
    os_msgq_receive(q1, &out, 0);

    os_msgq_delete(q1);
    os_ipc_deinit();
    TEST_PASS("Message queue duplicate/limit behavior works");
    return true;
}

static bool test_msgq_timeout_and_invalid_args(void)
{
    os_ipc_init();

    os_msgq_t q = os_msgq_create("timeout_q", sizeof(uint16_t), 1);
    TEST_ASSERT(q != NULL, "queue creation should succeed");

    uint16_t rx = 0;
    esp_err_t ret = os_msgq_receive(q, &rx, 1);
    TEST_ASSERT(ret == ESP_ERR_TIMEOUT, "receive on empty queue should timeout");

    ret = os_msgq_send(q, NULL, 0);
    TEST_ASSERT(ret == ESP_ERR_INVALID_ARG, "NULL message pointer must fail");

    os_msgq_delete(q);
    os_ipc_deinit();
    TEST_PASS("Message queue timeout/invalid argument behavior works");
    return true;
}

static bool test_event_group_flow(void)
{
    os_ipc_init();

    os_event_t ev = os_event_create("ev_basic");
    TEST_ASSERT(ev != NULL, "event group creation should succeed");

    esp_err_t ret = os_event_set(ev, 0x03);
    TEST_ASSERT(ret == ESP_OK, "event set should succeed");
    TEST_ASSERT((os_event_get(ev) & 0x03) == 0x03, "bits should be set");

    os_event_bits_t waited = os_event_wait(ev, 0x01, true, false, 0);
    TEST_ASSERT((waited & 0x01) == 0x01, "wait should observe bit 0");
    TEST_ASSERT((os_event_get(ev) & 0x01) == 0x00, "bit 0 should clear on exit");

    ret = os_event_delete(ev);
    TEST_ASSERT(ret == ESP_OK, "event group deletion should succeed");

    os_ipc_deinit();
    TEST_PASS("Event group set/wait/clear flow works");
    return true;
}

static bool test_shared_memory_flow(void)
{
    os_ipc_init();

    os_shm_t shm = os_shm_create("shm_basic", 64);
    TEST_ASSERT(shm != NULL, "shared memory creation should succeed");
    TEST_ASSERT(os_shm_get_size(shm) == 64, "shared memory size should match");

    uint8_t *ptr = (uint8_t *)os_shm_get_ptr(shm);
    TEST_ASSERT(ptr != NULL, "shared memory pointer should be valid");
    TEST_ASSERT(ptr[0] == 0, "shared memory must be zero-initialized");

    ptr[0] = 0x5A;
    os_shm_t found = os_shm_find("shm_basic");
    TEST_ASSERT(found == shm, "find by name should return same handle");
    TEST_ASSERT(((uint8_t *)os_shm_get_ptr(found))[0] == 0x5A, "shared memory write should persist");

    esp_err_t ret = os_shm_delete(shm);
    TEST_ASSERT(ret == ESP_OK, "shared memory delete should succeed");

    os_ipc_deinit();
    TEST_PASS("Shared memory create/find/delete flow works");
    return true;
}

void os_ipc_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS IPC TEST SUITE\n");
    printf("========================================\n");

    run_test("IPC Init Idempotent", test_ipc_init_idempotent);
    run_test("Message Queue Send/Receive", test_msgq_send_receive);
    run_test("Message Queue Duplicate and Limits", test_msgq_duplicate_and_limits);
    run_test("Message Queue Timeout and Invalid Args", test_msgq_timeout_and_invalid_args);
    run_test("Event Group Flow", test_event_group_flow);
    run_test("Shared Memory Flow", test_shared_memory_flow);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}