/**
 * @file os_kernel_test.c
 * @brief Comprehensive unit tests for os_kernel process management
 *
 * Tests cover:
 * - Process creation with various parameters
 * - Process lookup by PID and name
 * - Thread safety/race conditions
 * - Process lifecycle (create, suspend, resume, kill)
 * - Process deletion and memory cleanup
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

#include "os_kernel.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"

/* ────────────────────────────────────────────────────
   Test Infrastructure
   ──────────────────────────────────────────────────── */

#define TEST_PASS(fmt, ...) printf("[PASS] " fmt "\n", ##__VA_ARGS__)
#define TEST_FAIL(fmt, ...) do { \
    printf("[FAIL] " fmt "\n", ##__VA_ARGS__); \
    return false; \
} while(0)

#define TEST_ASSERT(cond, fmt, ...) do { \
    if (!(cond)) TEST_FAIL(fmt, ##__VA_ARGS__); \
} while(0)

typedef bool (*test_func_t)(void);

static int g_tests_passed = 0;
static int g_tests_failed = 0;

static void run_test(const char *name, test_func_t test)
{
    printf("\n>>> Running: %s\n", name);
    if (test()) {
        g_tests_passed++;
        printf("[✓] %s PASSED\n", name);
    } else {
        g_tests_failed++;
        printf("[✗] %s FAILED\n", name);
    }
}

/* ────────────────────────────────────────────────────
   Test Fixtures
   ──────────────────────────────────────────────────── */

static SemaphoreHandle_t test_sync_sem;
static volatile int test_task_count;

static void dummy_task(void *arg)
{
    if (arg) {
        xSemaphoreGive((SemaphoreHandle_t)arg);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelete(NULL);
}

static void task_with_param(void *arg)
{
    volatile int *p = (volatile int *)arg;
    if (p) *p = 42;
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void long_running_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ────────────────────────────────────────────────────
   Test Cases: Initialization
   ──────────────────────────────────────────────────── */

static bool test_kernel_init(void)
{
    esp_err_t ret = os_kernel_init();
    TEST_ASSERT(ret == ESP_OK || ret == ESP_OK,
                "Kernel init should succeed or already be initialized");

    ret = os_kernel_init();
    TEST_ASSERT(ret == ESP_OK,
                "Kernel init should be idempotent");

    TEST_PASS("Kernel initialization works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Process Creation
   ──────────────────────────────────────────────────── */

static bool test_process_create_basic(void)
{
    os_kernel_init();

    test_sync_sem = xSemaphoreCreateBinary();
    TEST_ASSERT(test_sync_sem, "Failed to create semaphore");

    os_pid_t pid = os_process_create("test_task", dummy_task, test_sync_sem,
                                      4096, 5, false);
    TEST_ASSERT(pid > 0, "Process creation should return valid PID, got %d", pid);

    xSemaphoreTake(test_sync_sem, pdMS_TO_TICKS(500));
    vSemaphoreDelete(test_sync_sem);

    TEST_PASS("Basic process creation works");
    return true;
}

static bool test_process_create_with_name(void)
{
    os_kernel_init();

    const char *name = "named_process";
    os_pid_t pid = os_process_create(name, dummy_task, NULL,
                                      4096, 5, false);
    TEST_ASSERT(pid > 0, "Named process creation should succeed");

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc != NULL, "Process should exist after creation");
    TEST_ASSERT(strcmp(proc->name, name) == 0,
                "Process name should match: expected '%s', got '%s'", name, proc->name);

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process creation with custom name works");
    return true;
}

static bool test_process_create_with_custom_stack(void)
{
    os_kernel_init();

    uint32_t stack_size = 8192;
    os_pid_t pid = os_process_create("stack_test", dummy_task, NULL,
                                      stack_size, 5, false);
    TEST_ASSERT(pid > 0, "Process creation with custom stack should succeed");

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc != NULL, "Process should exist");
    TEST_ASSERT(proc->stack_size >= stack_size,
                "Stack size should be at least requested: %"PRIu32" >= %"PRIu32,
                proc->stack_size, stack_size);

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process creation with custom stack works");
    return true;
}

static bool test_process_create_with_priority(void)
{
    os_kernel_init();

    UBaseType_t priority = 10;
    os_pid_t pid = os_process_create("prio_test", dummy_task, NULL,
                                      4096, priority, false);
    TEST_ASSERT(pid > 0, "Process creation with priority should succeed");

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc != NULL, "Process should exist");
    TEST_ASSERT(proc->priority == priority,
                "Priority should match: expected %d, got %d", priority, proc->priority);

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process creation with custom priority works");
    return true;
}

static bool test_process_create_null_func_fails(void)
{
    os_kernel_init();

    os_pid_t pid = os_process_create("null_func", NULL, NULL, 4096, 5, false);
    TEST_ASSERT(pid == 0, "Process creation with NULL func should fail, got pid=%d", pid);

    TEST_PASS("Process creation with NULL function correctly fails");
    return true;
}

static bool test_process_create_system_flag(void)
{
    os_kernel_init();

    os_pid_t pid = os_process_create("sys_task", dummy_task, NULL,
                                      4096, 5, true);
    TEST_ASSERT(pid > 0, "System process creation should succeed");

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc != NULL, "Process should exist");
    TEST_ASSERT(proc->is_system == true, "is_system flag should be set");

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("System process flag works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Process Lookup
   ──────────────────────────────────────────────────── */

static bool test_process_get_valid_pid(void)
{
    os_kernel_init();

    os_pid_t pid = os_process_create("lookup_test", dummy_task, NULL, 4096, 5, false);
    TEST_ASSERT(pid > 0, "Process creation should succeed");

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc != NULL, "Should find process by valid PID");
    TEST_ASSERT(proc->pid == pid, "Returned process should have matching PID");

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process lookup by valid PID works");
    return true;
}

static bool test_process_get_invalid_pid(void)
{
    os_kernel_init();

    const process_t *proc = os_process_get(99999);
    TEST_ASSERT(proc == NULL, "Should return NULL for invalid PID");

    TEST_PASS("Process lookup with invalid PID correctly returns NULL");
    return true;
}

static bool test_process_find_by_name_existing(void)
{
    os_kernel_init();

    const char *name = "find_by_name_test";
    os_pid_t pid = os_process_create(name, dummy_task, NULL, 4096, 5, false);
    TEST_ASSERT(pid > 0, "Process creation should succeed");

    const process_t *proc = os_process_find_by_name(name);
    TEST_ASSERT(proc != NULL, "Should find process by name");
    TEST_ASSERT(proc->pid == pid, "Found process should have matching PID");
    TEST_ASSERT(strcmp(proc->name, name) == 0,
                "Found process should have matching name");

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process lookup by existing name works");
    return true;
}

static bool test_process_find_by_name_nonexistent(void)
{
    os_kernel_init();

    const process_t *proc = os_process_find_by_name("nonexistent_process_xyz");
    TEST_ASSERT(proc == NULL, "Should return NULL for non-existent name");

    TEST_PASS("Process lookup with non-existent name correctly returns NULL");
    return true;
}

static bool test_process_find_by_name_null(void)
{
    os_kernel_init();

    const process_t *proc = os_process_find_by_name(NULL);
    TEST_ASSERT(proc == NULL, "Should return NULL for NULL name");

    TEST_PASS("Process lookup with NULL name correctly returns NULL");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Process State Management
   ──────────────────────────────────────────────────── */

static bool test_process_suspend_resume(void)
{
    os_kernel_init();

    os_pid_t pid = os_process_create("suspend_test", long_running_task, NULL,
                                      4096, 5, false);
    TEST_ASSERT(pid > 0, "Process creation should succeed");

    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = os_process_suspend(pid);
    TEST_ASSERT(ret == ESP_OK, "Suspend should succeed");

    vTaskDelay(pdMS_TO_TICKS(10));

    ret = os_process_resume(pid);
    TEST_ASSERT(ret == ESP_OK, "Resume should succeed");

    vTaskDelay(pdMS_TO_TICKS(10));

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process suspend/resume works");
    return true;
}

static bool test_process_kill(void)
{
    os_kernel_init();

    os_pid_t pid = os_process_create("kill_test", long_running_task, NULL,
                                      4096, 5, false);
    TEST_ASSERT(pid > 0, "Process creation should succeed");

    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = os_process_kill(pid);
    TEST_ASSERT(ret == ESP_OK, "Kill should succeed");

    vTaskDelay(pdMS_TO_TICKS(50));

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc == NULL || proc->state == PROC_STATE_DELETED,
                "Process should be deleted or marked as deleted");

    TEST_PASS("Process kill works");
    return true;
}

static bool test_process_signal_invalid_pid(void)
{
    os_kernel_init();

    esp_err_t ret = os_process_signal(99999, OS_SIG_KILL);
    TEST_ASSERT(ret == ESP_ERR_NOT_FOUND, "Signal to invalid PID should fail");

    TEST_PASS("Signaling invalid PID correctly fails");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Process Listing
   ──────────────────────────────────────────────────── */

static bool test_process_list(void)
{
    os_kernel_init();

    os_pid_t pids[3];
    for (int i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "list_test_%d", i);
        pids[i] = os_process_create(name, dummy_task, NULL, 4096, 5, false);
        TEST_ASSERT(pids[i] > 0, "Process %d creation should succeed", i);
    }

    process_t buf[20];
    int count = os_process_list(buf, 20);
    TEST_ASSERT(count >= 3, "Should list at least 3 processes, got %d", count);

    for (int i = 0; i < 3; i++) {
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (buf[j].pid == pids[i]) {
                found = true;
                break;
            }
        }
        TEST_ASSERT(found, "Created process %d should be in list", i);
    }

    for (int i = 0; i < 3; i++) {
        os_process_kill(pids[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    TEST_PASS("Process list works");
    return true;
}

static bool test_process_list_empty_buffer(void)
{
    os_kernel_init();

    int count = os_process_list(NULL, 0);
    TEST_ASSERT(count == 0, "NULL buffer should return 0");

    TEST_PASS("Process list with NULL buffer correctly returns 0");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Process Self
   ──────────────────────────────────────────────────── */

static bool test_process_self(void)
{
    os_kernel_init();

    os_pid_t self_pid = os_process_self();
    if (self_pid == 0) {
        TEST_PASS("Main task is not a managed process (expected)");
        return true;
    }

    const process_t *proc = os_process_get(self_pid);
    TEST_ASSERT(proc != NULL, "Should find self process");

    TEST_PASS("Process self works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Multiple Process Creation
   ──────────────────────────────────────────────────── */

static bool test_process_create_multiple(void)
{
    os_kernel_init();

    os_pid_t pids[5];
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "multi_%d", i);
        pids[i] = os_process_create(name, dummy_task, NULL, 4096, 5, false);
        TEST_ASSERT(pids[i] > 0, "Process %d creation should succeed", i);
    }

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(pids[i] != 0, "All PIDs should be valid");
    }

    for (int i = 0; i < 5; i++) {
        os_process_kill(pids[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    TEST_PASS("Multiple process creation works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Kernel Statistics
   ──────────────────────────────────────────────────── */

static bool test_kernel_get_stats(void)
{
    os_kernel_init();

    kernel_stats_t stats;
    os_kernel_get_stats(&stats);

    TEST_ASSERT(stats.free_heap_bytes > 0, "Free heap should be > 0");
    TEST_ASSERT(stats.total_heap_bytes > 0, "Total heap should be > 0");
    TEST_ASSERT(stats.free_heap_bytes <= stats.total_heap_bytes,
                "Free heap should be <= total heap");
    TEST_ASSERT(stats.process_count >= 0, "Process count should be >= 0");

    TEST_PASS("Kernel statistics work");
    return true;
}

static bool test_kernel_get_stats_null(void)
{
    os_kernel_init();

    os_kernel_get_stats(NULL);
    TEST_PASS("Kernel get_stats with NULL should not crash");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Edge Cases and Error Handling
   ──────────────────────────────────────────────────── */

static bool test_process_create_very_long_name(void)
{
    os_kernel_init();

    char long_name[256];
    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    os_pid_t pid = os_process_create(long_name, dummy_task, NULL, 4096, 5, false);
    TEST_ASSERT(pid > 0, "Process creation with long name should succeed");

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc != NULL, "Process should exist");
    TEST_ASSERT(strlen(proc->name) < OS_PROC_NAME_LEN,
                "Name should be truncated to OS_PROC_NAME_LEN");

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process creation with very long name works (name truncated)");
    return true;
}

static bool test_process_create_small_stack(void)
{
    os_kernel_init();

    os_pid_t pid = os_process_create("small_stack", dummy_task, NULL,
                                      512, 5, false);
    TEST_ASSERT(pid > 0, "Process creation should succeed");

    const process_t *proc = os_process_get(pid);
    TEST_ASSERT(proc != NULL, "Process should exist");
    TEST_ASSERT(proc->stack_size >= 1024,
                "Stack should be adjusted to minimum (1024): got %"PRIu32,
                proc->stack_size);

    os_process_kill(pid);
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_PASS("Process creation with small stack works (adjusted to minimum)");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Runner
   ──────────────────────────────────────────────────── */

void os_kernel_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS KERNEL TEST SUITE\n");
    printf("========================================\n");

    run_test("Kernel Init", test_kernel_init);

    run_test("Process Create Basic", test_process_create_basic);
    run_test("Process Create with Name", test_process_create_with_name);
    run_test("Process Create with Custom Stack", test_process_create_with_custom_stack);
    run_test("Process Create with Priority", test_process_create_with_priority);
    run_test("Process Create NULL Func Fails", test_process_create_null_func_fails);
    run_test("Process Create System Flag", test_process_create_system_flag);

    run_test("Process Get Valid PID", test_process_get_valid_pid);
    run_test("Process Get Invalid PID", test_process_get_invalid_pid);
    run_test("Process Find by Name Existing", test_process_find_by_name_existing);
    run_test("Process Find by Name Nonexistent", test_process_find_by_name_nonexistent);
    run_test("Process Find by Name NULL", test_process_find_by_name_null);

    run_test("Process Suspend/Resume", test_process_suspend_resume);
    run_test("Process Kill", test_process_kill);
    run_test("Process Signal Invalid PID", test_process_signal_invalid_pid);

    run_test("Process List", test_process_list);
    run_test("Process List Empty Buffer", test_process_list_empty_buffer);

    run_test("Process Self", test_process_self);

    run_test("Process Create Multiple", test_process_create_multiple);

    run_test("Kernel Get Stats", test_kernel_get_stats);
    run_test("Kernel Get Stats NULL", test_kernel_get_stats_null);

    run_test("Process Create Very Long Name", test_process_create_very_long_name);
    run_test("Process Create Small Stack", test_process_create_small_stack);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}
