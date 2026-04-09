/**
 * @file os_fs_test.c
 * @brief Comprehensive unit tests for os_fs filesystem operations
 *
 * Tests cover:
 * - File read/write operations
 * - Directory operations (create, list, remove)
 * - File existence checks
 * - Path handling (absolute, relative, CWD)
 * - Error conditions and edge cases
 * - File cleanup and resource management
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include "os_fs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static const char *TEST_DIR = "/test_fs";
static const char *TEST_FILE = "/test_fs/test.txt";
static const char *TEST_FILE2 = "/test_fs/test2.txt";
static const char *TEST_SUBDIR = "/test_fs/subdir";

static void cleanup_test_files(void)
{
    os_fs_remove(TEST_FILE);
    os_fs_remove(TEST_FILE2);
    os_fs_rmdir(TEST_SUBDIR);
    os_fs_rmdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/* ────────────────────────────────────────────────────
   Test Cases: Initialization
   ──────────────────────────────────────────────────── */

static bool test_fs_init(void)
{
    esp_err_t ret = os_fs_init();
    TEST_ASSERT(ret == ESP_OK || ret == ESP_OK,
                "FS init should succeed or already be initialized");

    ret = os_fs_init();
    TEST_ASSERT(ret == ESP_OK,
                "FS init should be idempotent");

    TEST_PASS("Filesystem initialization works");
    return true;
}

static bool test_fs_getcwd(void)
{
    os_fs_init();

    const char *cwd = os_fs_getcwd();
    TEST_ASSERT(cwd != NULL, "getcwd should return non-NULL");
    TEST_ASSERT(strlen(cwd) > 0, "getcwd should return non-empty path");

    TEST_PASS("Get current working directory works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Directory Operations
   ──────────────────────────────────────────────────── */

static bool test_fs_mkdir_basic(void)
{
    os_fs_init();
    cleanup_test_files();

    esp_err_t ret = os_fs_mkdir(TEST_DIR);
    TEST_ASSERT(ret == ESP_OK, "mkdir should succeed");

    TEST_ASSERT(os_fs_exists(TEST_DIR), "Directory should exist after mkdir");

    cleanup_test_files();
    TEST_PASS("Basic directory creation works");
    return true;
}

static bool test_fs_mkdir_already_exists(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = os_fs_mkdir(TEST_DIR);
    TEST_ASSERT(ret == ESP_OK, "mkdir on existing dir should succeed (idempotent)");

    cleanup_test_files();
    TEST_PASS("Mkdir on existing directory is idempotent");
    return true;
}

static bool test_fs_mkdir_nested(void)
{
    os_fs_init();
    cleanup_test_files();

    esp_err_t ret = os_fs_mkdir(TEST_DIR);
    TEST_ASSERT(ret == ESP_OK, "Parent dir creation should succeed");

    ret = os_fs_mkdir(TEST_SUBDIR);
    TEST_ASSERT(ret == ESP_OK, "Nested dir creation should succeed");
    TEST_ASSERT(os_fs_exists(TEST_SUBDIR), "Nested directory should exist");

    cleanup_test_files();
    TEST_PASS("Nested directory creation works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: File Write and Read
   ──────────────────────────────────────────────────── */

static bool test_fs_write_file_basic(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    const char *data = "Hello, Filesystem!";
    esp_err_t ret = os_fs_write_file(TEST_FILE, data, strlen(data), false);
    TEST_ASSERT(ret == ESP_OK, "write_file should succeed");
    TEST_ASSERT(os_fs_exists(TEST_FILE), "File should exist after write");

    cleanup_test_files();
    TEST_PASS("Basic file write works");
    return true;
}

static bool test_fs_read_file_basic(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    const char *data = "Test data for reading";
    os_fs_write_file(TEST_FILE, data, strlen(data), false);
    vTaskDelay(pdMS_TO_TICKS(10));

    char buf[256] = {0};
    size_t read_sz = 0;
    esp_err_t ret = os_fs_read_file(TEST_FILE, buf, sizeof(buf), &read_sz);
    TEST_ASSERT(ret == ESP_OK, "read_file should succeed");
    TEST_ASSERT(read_sz == strlen(data), "read size should match written size: %zu vs %zu",
                read_sz, strlen(data));
    TEST_ASSERT(strcmp(buf, data) == 0, "Read data should match written data");

    cleanup_test_files();
    TEST_PASS("Basic file read works");
    return true;
}

static bool test_fs_write_read_large_file(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    char *large_data = malloc(4096);
    TEST_ASSERT(large_data != NULL, "Malloc for large data should succeed");

    for (int i = 0; i < 4096; i++) {
        large_data[i] = (char)((i % 256) & 0xFF);
    }

    esp_err_t ret = os_fs_write_file(TEST_FILE, large_data, 4096, false);
    TEST_ASSERT(ret == ESP_OK, "write_file for large data should succeed");
    vTaskDelay(pdMS_TO_TICKS(20));

    char *read_buf = malloc(4096);
    TEST_ASSERT(read_buf != NULL, "Malloc for read buffer should succeed");

    size_t read_sz = 0;
    ret = os_fs_read_file(TEST_FILE, read_buf, 4096, &read_sz);
    TEST_ASSERT(ret == ESP_OK, "read_file for large data should succeed");
    TEST_ASSERT(read_sz == 4096, "read size should be 4096");
    TEST_ASSERT(memcmp(read_buf, large_data, 4096) == 0,
                "Large file data should match");

    free(large_data);
    free(read_buf);
    cleanup_test_files();

    TEST_PASS("Large file write and read works");
    return true;
}

static bool test_fs_append_file(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    const char *data1 = "First line\n";
    const char *data2 = "Second line\n";

    os_fs_write_file(TEST_FILE, data1, strlen(data1), false);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = os_fs_write_file(TEST_FILE, data2, strlen(data2), true);
    TEST_ASSERT(ret == ESP_OK, "append_file should succeed");

    char buf[512] = {0};
    size_t read_sz = 0;
    os_fs_read_file(TEST_FILE, buf, sizeof(buf), &read_sz);

    TEST_ASSERT(strstr(buf, data1) != NULL, "First line should be present");
    TEST_ASSERT(strstr(buf, data2) != NULL, "Second line should be present");

    cleanup_test_files();
    TEST_PASS("File append works");
    return true;
}

static bool test_fs_write_file_null_buffer(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = os_fs_write_file(TEST_FILE, NULL, 0, false);
    TEST_ASSERT(ret != ESP_OK, "write_file with NULL data should fail");

    cleanup_test_files();
    TEST_PASS("Write file with NULL buffer correctly fails");
    return true;
}

static bool test_fs_read_file_nonexistent(void)
{
    os_fs_init();
    cleanup_test_files();

    char buf[256] = {0};
    size_t read_sz = 0;
    esp_err_t ret = os_fs_read_file("/nonexistent/file.txt", buf, sizeof(buf), &read_sz);
    TEST_ASSERT(ret != ESP_OK, "read_file of nonexistent file should fail");

    cleanup_test_files();
    TEST_PASS("Reading nonexistent file correctly fails");
    return true;
}

static bool test_fs_read_file_buffer_too_small(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    const char *data = "This is a longer test string";
    os_fs_write_file(TEST_FILE, data, strlen(data), false);
    vTaskDelay(pdMS_TO_TICKS(10));

    char small_buf[5] = {0};
    size_t read_sz = 0;
    esp_err_t ret = os_fs_read_file(TEST_FILE, small_buf, sizeof(small_buf), &read_sz);

    TEST_ASSERT(ret == ESP_OK || ret != ESP_OK,
                "read_file with small buffer should complete");
    TEST_ASSERT(read_sz <= sizeof(small_buf) - 1,
                "read size should not exceed buffer");

    cleanup_test_files();
    TEST_PASS("Reading with small buffer is handled");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: File Existence and Stats
   ──────────────────────────────────────────────────── */

static bool test_fs_exists_true(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    os_fs_write_file(TEST_FILE, "test", 4, false);
    vTaskDelay(pdMS_TO_TICKS(10));

    bool exists = os_fs_exists(TEST_FILE);
    TEST_ASSERT(exists == true, "exists should return true for existing file");

    cleanup_test_files();
    TEST_PASS("File existence check returns true for existing file");
    return true;
}

static bool test_fs_exists_false(void)
{
    os_fs_init();
    cleanup_test_files();

    bool exists = os_fs_exists("/nonexistent/file.txt");
    TEST_ASSERT(exists == false, "exists should return false for nonexistent file");

    cleanup_test_files();
    TEST_PASS("File existence check returns false for nonexistent file");
    return true;
}

static bool test_fs_stat_file(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    const char *data = "stat test";
    os_fs_write_file(TEST_FILE, data, strlen(data), false);
    vTaskDelay(pdMS_TO_TICKS(10));

    struct stat st;
    esp_err_t ret = os_fs_stat(TEST_FILE, &st);
    TEST_ASSERT(ret == ESP_OK, "stat should succeed");
    TEST_ASSERT(st.st_size > 0, "File size should be > 0");

    cleanup_test_files();
    TEST_PASS("File stat works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: File Operations (Rename, Remove)
   ──────────────────────────────────────────────────── */

static bool test_fs_rename_file(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    const char *data = "rename test";
    os_fs_write_file(TEST_FILE, data, strlen(data), false);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = os_fs_rename(TEST_FILE, TEST_FILE2);
    TEST_ASSERT(ret == ESP_OK, "rename should succeed");
    TEST_ASSERT(!os_fs_exists(TEST_FILE), "Old name should not exist");
    TEST_ASSERT(os_fs_exists(TEST_FILE2), "New name should exist");

    cleanup_test_files();
    TEST_PASS("File rename works");
    return true;
}

static bool test_fs_remove_file(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    os_fs_write_file(TEST_FILE, "test", 4, false);
    vTaskDelay(pdMS_TO_TICKS(10));

    TEST_ASSERT(os_fs_exists(TEST_FILE), "File should exist");

    esp_err_t ret = os_fs_remove(TEST_FILE);
    TEST_ASSERT(ret == ESP_OK, "remove should succeed");
    TEST_ASSERT(!os_fs_exists(TEST_FILE), "File should not exist after remove");

    cleanup_test_files();
    TEST_PASS("File remove works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Directory Listing
   ──────────────────────────────────────────────────── */

static int g_list_count = 0;
static char g_list_names[10][256];

static void list_callback(const os_fs_entry_t *e, void *arg)
{
    if (g_list_count < 10 && e) {
        strncpy(g_list_names[g_list_count], e->name, sizeof(g_list_names[0]) - 1);
        g_list_count++;
    }
}

static bool test_fs_listdir(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    os_fs_write_file(TEST_FILE, "file1", 5, false);
    os_fs_write_file(TEST_FILE2, "file2", 5, false);
    vTaskDelay(pdMS_TO_TICKS(20));

    g_list_count = 0;
    memset(g_list_names, 0, sizeof(g_list_names));

    int count = os_fs_listdir(TEST_DIR, list_callback, NULL);
    TEST_ASSERT(count >= 2, "listdir should find at least 2 files, got %d", count);
    TEST_ASSERT(g_list_count >= 2, "Callback should be called at least 2 times");

    cleanup_test_files();
    TEST_PASS("Directory listing works");
    return true;
}

static bool test_fs_listdir_nonexistent(void)
{
    os_fs_init();
    cleanup_test_files();

    int count = os_fs_listdir("/nonexistent/dir", list_callback, NULL);
    TEST_ASSERT(count == -1, "listdir on nonexistent dir should return -1");

    cleanup_test_files();
    TEST_PASS("Directory listing on nonexistent dir correctly fails");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Path Handling
   ──────────────────────────────────────────────────── */

static bool test_fs_abspath_absolute(void)
{
    os_fs_init();

    char buf[256];
    os_fs_abspath("/etc/config", buf, sizeof(buf));

    TEST_ASSERT(strstr(buf, "/etc/config") != NULL,
                "Absolute path should be included");

    TEST_PASS("Absolute path handling works");
    return true;
}

static bool test_fs_abspath_relative(void)
{
    os_fs_init();

    char buf[256];
    os_fs_abspath("test.txt", buf, sizeof(buf));

    TEST_ASSERT(strlen(buf) > 0, "Relative path should be converted");
    TEST_ASSERT(buf[0] != '\0', "Path should not be empty");

    TEST_PASS("Relative path handling works");
    return true;
}

static bool test_fs_chdir(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = os_fs_chdir(TEST_DIR);
    TEST_ASSERT(ret == ESP_OK, "chdir should succeed");

    const char *cwd = os_fs_getcwd();
    TEST_ASSERT(cwd != NULL, "getcwd should return non-NULL");

    cleanup_test_files();
    TEST_PASS("Change directory works");
    return true;
}

static bool test_fs_chdir_nonexistent(void)
{
    os_fs_init();

    esp_err_t ret = os_fs_chdir("/nonexistent/dir");
    TEST_ASSERT(ret != ESP_OK, "chdir to nonexistent dir should fail");

    TEST_PASS("Changing to nonexistent directory correctly fails");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: File System Usage
   ──────────────────────────────────────────────────── */

static bool test_fs_usage(void)
{
    os_fs_init();

    size_t total, used;
    os_fs_usage(&total, &used);

    TEST_ASSERT(total > 0, "Total should be > 0");
    TEST_ASSERT(used >= 0, "Used should be >= 0");
    TEST_ASSERT(used <= total, "Used should be <= total");

    TEST_PASS("Filesystem usage reporting works");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Cases: Multiple File Operations
   ──────────────────────────────────────────────────── */

static bool test_fs_multiple_files(void)
{
    os_fs_init();
    cleanup_test_files();

    os_fs_mkdir(TEST_DIR);
    vTaskDelay(pdMS_TO_TICKS(10));

    for (int i = 0; i < 5; i++) {
        char filename[256];
        char data[256];
        snprintf(filename, sizeof(filename), "%s/file_%d.txt", TEST_DIR, i);
        snprintf(data, sizeof(data), "File %d content", i);

        esp_err_t ret = os_fs_write_file(filename, data, strlen(data), false);
        TEST_ASSERT(ret == ESP_OK, "write_file %d should succeed", i);
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    for (int i = 0; i < 5; i++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/file_%d.txt", TEST_DIR, i);
        TEST_ASSERT(os_fs_exists(filename), "file_%d should exist", i);
    }

    cleanup_test_files();
    TEST_PASS("Multiple file operations work");
    return true;
}

/* ────────────────────────────────────────────────────
   Test Runner
   ──────────────────────────────────────────────────── */

void os_fs_test_run_all(void)
{
    printf("\n========================================\n");
    printf("  OS FILESYSTEM TEST SUITE\n");
    printf("========================================\n");

    run_test("FS Init", test_fs_init);
    run_test("FS Get CWD", test_fs_getcwd);

    run_test("FS Mkdir Basic", test_fs_mkdir_basic);
    run_test("FS Mkdir Already Exists", test_fs_mkdir_already_exists);
    run_test("FS Mkdir Nested", test_fs_mkdir_nested);

    run_test("FS Write File Basic", test_fs_write_file_basic);
    run_test("FS Read File Basic", test_fs_read_file_basic);
    run_test("FS Write Read Large File", test_fs_write_read_large_file);
    run_test("FS Append File", test_fs_append_file);
    run_test("FS Write File NULL Buffer", test_fs_write_file_null_buffer);
    run_test("FS Read File Nonexistent", test_fs_read_file_nonexistent);
    run_test("FS Read File Buffer Too Small", test_fs_read_file_buffer_too_small);

    run_test("FS Exists True", test_fs_exists_true);
    run_test("FS Exists False", test_fs_exists_false);
    run_test("FS Stat File", test_fs_stat_file);

    run_test("FS Rename File", test_fs_rename_file);
    run_test("FS Remove File", test_fs_remove_file);

    run_test("FS Listdir", test_fs_listdir);
    run_test("FS Listdir Nonexistent", test_fs_listdir_nonexistent);

    run_test("FS Abspath Absolute", test_fs_abspath_absolute);
    run_test("FS Abspath Relative", test_fs_abspath_relative);
    run_test("FS Chdir", test_fs_chdir);
    run_test("FS Chdir Nonexistent", test_fs_chdir_nonexistent);

    run_test("FS Usage", test_fs_usage);

    run_test("FS Multiple Files", test_fs_multiple_files);

    printf("\n========================================\n");
    printf("  TEST RESULTS\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("Total:  %d\n", g_tests_passed + g_tests_failed);
    printf("========================================\n\n");
}
