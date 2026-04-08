#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   Mount Points & Limits
   ──────────────────────────────────────────────── */
#define OS_FS_MOUNT_POINT   "/spiffs"
#define OS_FS_MAX_FILES     10
#define OS_FS_PATH_MAX      128
#define OS_FS_CWD_MAX       128

/* ────────────────────────────────────────────────
   Directory Entry (ls)
   ──────────────────────────────────────────────── */
typedef struct {
    char     name[OS_FS_PATH_MAX];
    bool     is_dir;
    uint32_t size;
    time_t   mtime;
} os_fs_entry_t;

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */

/** Mount SPIFFS and initialize VFS */
esp_err_t os_fs_init(void);
void      os_fs_deinit(void);

/** Working directory management */
esp_err_t   os_fs_chdir(const char *path);
const char *os_fs_getcwd(void);

/** Resolve relative path to absolute VFS path */
void os_fs_abspath(const char *rel, char *out, size_t out_sz);

/** Directory operations */
esp_err_t os_fs_mkdir(const char *path);
esp_err_t os_fs_rmdir(const char *path);

/** File operations */
esp_err_t os_fs_remove(const char *path);
esp_err_t os_fs_rename(const char *src, const char *dst);

/** List directory.  Calls cb for each entry; returns entry count or -1 on error. */
typedef void (*os_fs_ls_cb_t)(const os_fs_entry_t *e, void *arg);
int os_fs_listdir(const char *path, os_fs_ls_cb_t cb, void *arg);

/** File I/O helpers */
esp_err_t os_fs_read_file(const char *path, char *buf, size_t buf_sz, size_t *read_sz);
esp_err_t os_fs_write_file(const char *path, const char *data, size_t len, bool append);

/** File stats */
esp_err_t os_fs_stat(const char *path, struct stat *st);
bool      os_fs_exists(const char *path);

/** Get filesystem usage */
void os_fs_usage(size_t *total, size_t *used);

/** Print ls output to fd */
void os_fs_print_ls(int fd, const char *path);

#ifdef __cplusplus
}
#endif
