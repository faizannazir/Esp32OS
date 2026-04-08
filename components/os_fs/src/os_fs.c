#include "os_fs.h"
#include "os_logging.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <sys/unistd.h>

#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "driver/uart.h"
#include "lwip/sockets.h"

#define TAG "OS_FS"

/* ────────────────────────────────────────────────
   State
   ──────────────────────────────────────────────── */
static struct {
    bool initialised;
    char cwd[OS_FS_CWD_MAX];
} s_fs = {
    .initialised = false,
    .cwd = "/"
};

/* ────────────────────────────────────────────────
   Internal helpers
   ──────────────────────────────────────────────── */
static void fd_write(int fd, const char *s)
{
    if (fd < 0) {
        uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, s, strlen(s));
    } else {
        send(fd, s, strlen(s), 0);
    }
}

static void fd_printf(int fd, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static void fd_printf(int fd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fd_write(fd, buf);
}

/* ────────────────────────────────────────────────
   Init / Deinit
   ──────────────────────────────────────────────── */
esp_err_t os_fs_init(void)
{
    if (s_fs.initialised) return ESP_OK;

    esp_vfs_spiffs_conf_t cfg = {
        .base_path              = OS_FS_MOUNT_POINT,
        .partition_label        = NULL,   /* uses "spiffs" partition  */
        .max_files              = OS_FS_MAX_FILES,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&cfg);
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    strncpy(s_fs.cwd, "/", sizeof(s_fs.cwd));
    s_fs.initialised = true;

    size_t total = 0, used = 0;
    os_fs_usage(&total, &used);
    OS_LOGI(TAG, "SPIFFS mounted at %s  (%zu kB used / %zu kB total)",
            OS_FS_MOUNT_POINT, used / 1024, total / 1024);

    /* Create standard directories */
    os_fs_mkdir("/logs");
    os_fs_mkdir("/tmp");
    os_fs_mkdir("/etc");

    return ESP_OK;
}

void os_fs_deinit(void)
{
    if (!s_fs.initialised) return;
    esp_vfs_spiffs_unregister(NULL);
    s_fs.initialised = false;
    OS_LOGI(TAG, "SPIFFS unmounted");
}

/* ────────────────────────────────────────────────
   CWD Management
   ──────────────────────────────────────────────── */
const char *os_fs_getcwd(void)
{
    return s_fs.cwd;
}

void os_fs_abspath(const char *rel, char *out, size_t out_sz)
{
    if (!rel || !out || out_sz == 0) return;

    if (rel[0] == '/') {
        /* Absolute path: prepend mount point */
        if (strncmp(rel, OS_FS_MOUNT_POINT, strlen(OS_FS_MOUNT_POINT)) == 0) {
            strncpy(out, rel, out_sz - 1);
        } else {
            snprintf(out, out_sz, "%s%s", OS_FS_MOUNT_POINT, rel);
        }
    } else {
        /* Relative to CWD */
        char full_cwd[OS_FS_CWD_MAX + 32];
        snprintf(full_cwd, sizeof(full_cwd), "%s%s",
                 OS_FS_MOUNT_POINT, s_fs.cwd);
        if (full_cwd[strlen(full_cwd)-1] != '/') {
            strncat(full_cwd, "/", sizeof(full_cwd) - strlen(full_cwd) - 1);
        }
        snprintf(out, out_sz, "%s%s", full_cwd, rel);
    }
    out[out_sz - 1] = '\0';
}

esp_err_t os_fs_chdir(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;

    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));

    struct stat st;
    if (stat(abs, &st) != 0) return ESP_ERR_NOT_FOUND;
    if (!S_ISDIR(st.st_mode)) return ESP_ERR_INVALID_ARG;

    /* Store CWD without mount-point prefix */
    const char *mp = OS_FS_MOUNT_POINT;
    size_t mplen   = strlen(mp);
    const char *rel = (strncmp(abs, mp, mplen) == 0) ? abs + mplen : abs;
    strncpy(s_fs.cwd, rel[0] ? rel : "/", sizeof(s_fs.cwd) - 1);
    s_fs.cwd[sizeof(s_fs.cwd)-1] = '\0';
    return ESP_OK;
}

/* ────────────────────────────────────────────────
   Directory / File Operations
   ──────────────────────────────────────────────── */
esp_err_t os_fs_mkdir(const char *path)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));
    if (mkdir(abs, 0755) != 0) {
        if (errno == EEXIST) {
            return ESP_OK;
        }

        if (errno == ENOTSUP || errno == ENOSYS) {
            char marker[OS_FS_PATH_MAX + 24];
            int n = snprintf(marker, sizeof(marker), "%s/.dir", abs);
            if (n <= 0 || (size_t)n >= sizeof(marker)) {
                OS_LOGE(TAG, "mkdir '%s': path too long", abs);
                return ESP_FAIL;
            }

            FILE *f = fopen(marker, "w");
            if (!f) {
                OS_LOGE(TAG, "mkdir fallback '%s': %s", marker, strerror(errno));
                return ESP_FAIL;
            }
            fclose(f);
            return ESP_OK;
        }

        OS_LOGE(TAG, "mkdir '%s': %s", abs, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t os_fs_rmdir(const char *path)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));
    if (rmdir(abs) != 0) {
        OS_LOGE(TAG, "rmdir '%s': %s", abs, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t os_fs_remove(const char *path)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));
    if (unlink(abs) != 0) {
        OS_LOGE(TAG, "unlink '%s': %s", abs, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t os_fs_rename(const char *src, const char *dst)
{
    char asrc[OS_FS_PATH_MAX + 16], adst[OS_FS_PATH_MAX + 16];
    os_fs_abspath(src, asrc, sizeof(asrc));
    os_fs_abspath(dst, adst, sizeof(adst));
    if (rename(asrc, adst) != 0) return ESP_FAIL;
    return ESP_OK;
}

bool os_fs_exists(const char *path)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));
    struct stat st;
    return (stat(abs, &st) == 0);
}

esp_err_t os_fs_stat(const char *path, struct stat *st)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));
    return (stat(abs, st) == 0) ? ESP_OK : ESP_FAIL;
}

/* ────────────────────────────────────────────────
   List Directory
   ──────────────────────────────────────────────── */
int os_fs_listdir(const char *path, os_fs_ls_cb_t cb, void *arg)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));

    DIR *dir = opendir(abs);
    if (!dir) {
        OS_LOGW(TAG, "opendir '%s': %s", abs, strerror(errno));
        return -1;
    }

    int count = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        os_fs_entry_t e = {0};
        strncpy(e.name, de->d_name, sizeof(e.name) - 1);

        char full[OS_FS_PATH_MAX + 32];
        size_t abs_len = strlen(abs);
        size_t name_len = strlen(de->d_name);
        if (abs_len + 1 + name_len >= sizeof(full)) {
            OS_LOGW(TAG, "Path too long while listing: %s/%s", abs, de->d_name);
            continue;
        }
        memcpy(full, abs, abs_len);
        full[abs_len] = '/';
        memcpy(full + abs_len + 1, de->d_name, name_len + 1);
        struct stat st;
        if (stat(full, &st) == 0) {
            e.is_dir = S_ISDIR(st.st_mode);
            e.size   = st.st_size;
            e.mtime  = st.st_mtime;
        }

        if (cb) cb(&e, arg);
        count++;
    }
    closedir(dir);
    return count;
}

/* ────────────────────────────────────────────────
   File I/O
   ──────────────────────────────────────────────── */
esp_err_t os_fs_read_file(const char *path, char *buf, size_t buf_sz, size_t *read_sz)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));

    FILE *f = fopen(abs, "r");
    if (!f) {
        OS_LOGE(TAG, "Cannot open '%s': %s", abs, strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = fread(buf, 1, buf_sz - 1, f);
    buf[n] = '\0';
    if (read_sz) *read_sz = n;
    fclose(f);
    return ESP_OK;
}

esp_err_t os_fs_write_file(const char *path, const char *data, size_t len, bool append)
{
    char abs[OS_FS_PATH_MAX + 16];
    os_fs_abspath(path, abs, sizeof(abs));

    FILE *f = fopen(abs, append ? "a" : "w");
    if (!f) {
        OS_LOGE(TAG, "Cannot write '%s': %s", abs, strerror(errno));
        return ESP_FAIL;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    return ESP_OK;
}

/* ────────────────────────────────────────────────
   Usage
   ──────────────────────────────────────────────── */
void os_fs_usage(size_t *total, size_t *used)
{
    esp_spiffs_info(NULL, total, used);
}

/* ────────────────────────────────────────────────
   Pretty ls
   ──────────────────────────────────────────────── */
typedef struct { int fd; int count; } ls_ctx_t;

static void ls_cb(const os_fs_entry_t *e, void *arg)
{
    ls_ctx_t *ctx = arg;
    if (e->is_dir) {
        fd_printf(ctx->fd, "\033[34m%-30s\033[0m  <DIR>\r\n", e->name);
    } else {
        fd_printf(ctx->fd, "%-30s  %8"PRIu32" B\r\n", e->name, e->size);
    }
    ctx->count++;
}

void os_fs_print_ls(int fd, const char *path)
{
    ls_ctx_t ctx = { .fd = fd, .count = 0 };
    int n = os_fs_listdir(path ? path : s_fs.cwd, ls_cb, &ctx);
    if (n < 0) {
        fd_printf(fd, "ls: cannot access '%s'\r\n", path);
        return;
    }
    fd_printf(fd, "\r\n%d item(s)\r\n", ctx.count);
}
