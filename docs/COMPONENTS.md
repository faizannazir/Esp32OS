# ESP32OS Component API Reference

---

## os_logging

**Header:** `components/os_logging/include/os_logging.h`

### Initialisation
```c
esp_err_t os_log_init(void);
void      os_log_deinit(void);
```

### Writing logs
```c
// Low-level write
void os_log_write(os_log_level_t level, const char *tag, const char *fmt, ...);

// Convenience macros
OS_LOGI(tag, fmt, ...)   // INFO
OS_LOGW(tag, fmt, ...)   // WARNING
OS_LOGE(tag, fmt, ...)   // ERROR
OS_LOGD(tag, fmt, ...)   // DEBUG
```

### Configuration
```c
void os_log_set_level(os_log_level_t level);
os_log_level_t os_log_get_level(void);

void os_log_set_file_output(bool enable);  // enable/disable SPIFFS file
bool os_log_get_file_output(void);
```

### Retrieval
```c
// Dump last N lines to fd (-1=UART, else socket)
void os_log_dump(int fd, uint32_t lines);

// Copy recent entries into user buffer
int  os_log_get_recent(os_log_entry_t *buf, int max_count);

// Force flush file
void os_log_flush(void);
```

### Log Level Values
| Constant | Value | Shell name |
|----------|-------|------------|
| `OS_LOG_DEBUG` | 0 | `debug` |
| `OS_LOG_INFO` | 1 | `info` |
| `OS_LOG_WARN` | 2 | `warn` |
| `OS_LOG_ERROR` | 3 | `error` |
| `OS_LOG_NONE` | 4 | `none` |

---

## os_kernel

**Header:** `components/os_kernel/include/os_kernel.h`

### Initialisation
```c
esp_err_t os_kernel_init(void);
```

### Process Creation
```c
os_pid_t os_process_create(
    const char   *name,       // display name (max 24 chars)
    os_task_fn_t  func,       // void (*)(void *arg)
    void         *arg,        // argument passed to task
    uint32_t      stack_size, // bytes (min 1024, default 4096)
    UBaseType_t   priority,   // FreeRTOS priority (1–24)
    bool          is_system   // mark as system task in ps
);
// Returns PID (>0) on success, 0 on failure
```

### Process Control
```c
esp_err_t os_process_kill(os_pid_t pid);
esp_err_t os_process_suspend(os_pid_t pid);
esp_err_t os_process_resume(os_pid_t pid);
esp_err_t os_process_signal(os_pid_t pid, os_signal_t sig);
```

### Process Query
```c
int               os_process_list(process_t *buf, int max_count);
const process_t  *os_process_get(os_pid_t pid);
const process_t  *os_process_find_by_name(const char *name);
os_pid_t          os_process_self(void);       // current task PID
```

### System Stats
```c
void os_kernel_get_stats(kernel_stats_t *out);
void os_kernel_print_ps(int fd);
void os_kernel_print_top(int fd);
```

### Watchdog
```c
void os_watchdog_enable(uint32_t timeout_ms);
void os_watchdog_disable(void);
void os_watchdog_feed(void);
```

### process_t Fields
```c
typedef struct {
    os_pid_t       pid;                 // Process ID
    char           name[24];            // Task name
    TaskHandle_t   handle;              // FreeRTOS handle
    proc_state_t   state;               // R/S/D/T/Z
    UBaseType_t    priority;            // Current priority
    uint32_t       stack_size;          // Allocated stack bytes
    uint32_t       stack_high_water;    // Min free stack bytes seen
    TickType_t     created_at;          // Tick count at creation
    bool           is_system;           // System/user flag
} process_t;
```

---

## os_fs

**Header:** `components/os_fs/include/os_fs.h`

### Initialisation
```c
esp_err_t os_fs_init(void);    // Mount SPIFFS, create /logs /tmp /etc
void      os_fs_deinit(void);  // Unmount
```

### Working Directory
```c
esp_err_t   os_fs_chdir(const char *path);
const char *os_fs_getcwd(void);
void        os_fs_abspath(const char *rel, char *out, size_t out_sz);
```

### File Operations
```c
esp_err_t os_fs_read_file(const char *path, char *buf, size_t buf_sz, size_t *read_sz);
esp_err_t os_fs_write_file(const char *path, const char *data, size_t len, bool append);
esp_err_t os_fs_remove(const char *path);
esp_err_t os_fs_rename(const char *src, const char *dst);
bool      os_fs_exists(const char *path);
esp_err_t os_fs_stat(const char *path, struct stat *st);
```

### Directory Operations
```c
esp_err_t os_fs_mkdir(const char *path);
esp_err_t os_fs_rmdir(const char *path);

// Iterate a directory; calls cb for each entry
int os_fs_listdir(const char *path, os_fs_ls_cb_t cb, void *arg);

// Print coloured ls output to fd
void os_fs_print_ls(int fd, const char *path);
```

### Usage
```c
void os_fs_usage(size_t *total, size_t *used);
```

**Path resolution rules:**
- Paths starting with `/` are absolute: `/etc/config` → `/spiffs/etc/config`
- Relative paths combine with CWD: `notes.txt` + CWD `/tmp` → `/spiffs/tmp/notes.txt`
- The SPIFFS mount point prefix `/spiffs` is transparent to callers

---

## os_networking

**Header:** `components/os_networking/include/os_networking.h`

### Initialisation
```c
esp_err_t os_net_init(void);
// Initialises TCP/IP stack, WiFi driver, event handlers
// Attempts auto-connect from NVS credentials on boot
```

### WiFi
```c
// Scan (returns count found, fills results array)
int os_wifi_scan(os_wifi_scan_result_t *results, int max_results);

// Connect (blocks up to 15 s)
esp_err_t os_wifi_connect(const char *ssid, const char *password);

// Disconnect
void os_wifi_disconnect(void);

// Status
void os_wifi_get_status(os_net_status_t *st);

// Persist credentials for auto-connect
esp_err_t os_wifi_save_credentials(const char *ssid, const char *password);
```

### Utilities
```c
// ICMP ping
esp_err_t os_ping(const char *host, int count, os_ping_result_t *result);

// HTTP GET (returns bytes read, -1 on error)
int os_http_get(const char *url, char *buf, size_t buf_sz);
```

---

## os_drivers (HAL)

**Header:** `components/os_drivers/include/os_drivers.h`

### GPIO
```c
esp_err_t gpio_driver_init(void);
esp_err_t gpio_driver_set_dir(int pin, gpio_dir_t dir);
int       gpio_driver_read(int pin);          // 0 or 1, -1 on error
esp_err_t gpio_driver_write(int pin, int val);
void      gpio_driver_print_info(int fd);
```

**gpio_dir_t values:** `GPIO_DIR_INPUT`, `GPIO_DIR_OUTPUT`, `GPIO_DIR_INPUT_PULLUP`, `GPIO_DIR_INPUT_PULLDOWN`, `GPIO_DIR_OUTPUT_OD`

### ADC
```c
esp_err_t adc_driver_init(void);
int       adc_driver_read_raw(int channel);   // 0–4095, -1 on error
int       adc_driver_read_mv(int channel);    // millivolts, -1 on error
```

Channels 0–7 map to ADC1_CH0–CH7 (GPIO36–GPIO35 on ESP32).

### I2C
```c
esp_err_t i2c_driver_init(int sda, int scl, uint32_t freq_hz);
void      i2c_driver_deinit(void);
void      i2c_driver_scan(int sda, int scl, int fd);
esp_err_t i2c_driver_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
esp_err_t i2c_driver_write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);
```

### SPI
```c
esp_err_t spi_driver_init(int mosi, int miso, int clk, int cs, uint32_t freq_hz);
void      spi_driver_deinit(void);
esp_err_t spi_driver_transfer(const uint8_t *tx, uint8_t *rx, size_t len);
```

### HAL All-in-One
```c
esp_err_t os_hal_init(void);
// Calls: gpio_driver_init, adc_driver_init, i2c_driver_init(default pins)
```

---

## os_shell

**Header:** `components/os_shell/include/os_shell.h`

### Initialisation
```c
esp_err_t shell_init(void);               // Register table setup
esp_err_t shell_start_uart(void);         // Start UART reader task
esp_err_t shell_start_telnet(uint16_t port); // Start Telnet server task
```

### Command Registration
```c
typedef int (*shell_cmd_fn_t)(int fd, int argc, char **argv);

typedef struct {
    const char    *name;         // Command name (no spaces)
    const char    *description;  // One-line description for help
    const char    *usage;        // Usage string shown by --help
    shell_cmd_fn_t handler;      // Handler function
} shell_command_t;

esp_err_t shell_register_command(const shell_command_t *cmd);
```

**Handler return values:**
- `SHELL_CMD_OK` (0) — success
- `SHELL_CMD_ERROR` (-1) — failure (no disconnect)
- `SHELL_CMD_EXIT` (-99) — close the session (Telnet) or continue (UART)

### I/O
```c
void shell_write(int fd, const char *str);
void shell_printf(int fd, const char *fmt, ...);

```

### Host Integration Validation

For shell command workflows that do not require hardware, run:

```bash
python3 tools/test_shell_host_integration.py
```

This validates behavior for:

- `env`, `export`, `unset`, `printenv`
- `run`, `at`, `every`, `jobs`, `killjob`

`fd == -1` → write to UART; `fd >= 0` → write to Telnet socket.

### Execution
```c
int shell_execute(int fd, const char *line);
const shell_command_t *shell_find_command(const char *name);
void shell_print_help(int fd);
```

### Quick Command Registration Macro
```c
static const shell_command_t my_cmd =
    SHELL_CMD_ENTRY("mycmd", "My description", "mycmd [args]", my_handler);

shell_register_command(&my_cmd);
```

---

## os_timer

**Header:** `components/os_timer/include/os_timer.h`

### Initialisation
```c
esp_err_t os_timer_init(void);
void      os_timer_deinit(void);
```

### Timer API
```c
typedef void (*os_timer_cb_t)(void *arg);

typedef struct {
    const char *name;
    uint32_t    period_ms;
    bool        reload;
    os_timer_cb_t callback;
    void       *arg;
} os_timer_config_t;

typedef struct os_timer_s *os_timer_t;

os_timer_t os_timer_create(const os_timer_config_t *config);
esp_err_t  os_timer_delete(os_timer_t timer);
esp_err_t  os_timer_start(os_timer_t timer);
esp_err_t  os_timer_stop(os_timer_t timer);
esp_err_t  os_timer_restart(os_timer_t timer, uint32_t new_period_ms);
os_timer_t os_timer_find(const char *name);
void       os_timer_list(int fd);
```

---

## os_env

**Header:** `components/os_env/include/os_env.h`

### Initialisation
```c
esp_err_t os_env_init(void);
void      os_env_deinit(void);
```

### Variable API
```c
esp_err_t os_env_set(const char *name, const char *value);
esp_err_t os_env_get(const char *name, char *buf, size_t buf_sz);
esp_err_t os_env_unset(const char *name);
esp_err_t os_env_clear(void);
int       os_env_list(int fd);
size_t    os_env_expand(const char *input, char *output, size_t out_sz);
```

---

## os_scheduler

**Header:** `components/os_scheduler/include/os_scheduler.h`

### Initialisation
```c
esp_err_t os_scheduler_init(void);
void      os_scheduler_deinit(void);
```

### Job API
```c
esp_err_t os_scheduler_run_background(const char *name, const char *command, int fd);
esp_err_t os_scheduler_schedule(const char *name, const char *command, uint32_t delay_ms, bool repeat, int fd);
esp_err_t os_scheduler_cancel(const char *name);
void      os_scheduler_list(int fd);
bool      os_scheduler_is_running(const char *name);
```
