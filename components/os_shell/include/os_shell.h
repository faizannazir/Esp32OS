#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   Shell Configuration
   ──────────────────────────────────────────────── */
#define SHELL_MAX_LINE_LEN     256
#define SHELL_HISTORY_DEPTH    20
#define SHELL_MAX_ARGS         16
#define SHELL_MAX_COMMANDS     64
#define SHELL_PROMPT           "esp32os> "
#define SHELL_BANNER \
    "\r\n\033[1;36m" \
    "  _____  ____  ____  ____  ____  ___  ____\r\n" \
    " | ____|/ ___||  _ \\|___ \\|___ \\/ _ \\/ ___|\r\n" \
    " |  _|  \\___ \\| |_) | __) | __) | | | \\___ \\\r\n" \
    " | |___  ___) |  __/ / __/ / __/| |_| |___) |\r\n" \
    " |_____|____/|_|   |_____|_____|\\___/|____/\r\n" \
    "\033[0m\r\n" \
    "  ESP32 Embedded OS  v1.0.0\r\n" \
    "  Type 'help' for commands\r\n\r\n"

/* ────────────────────────────────────────────────
   Transport
   ──────────────────────────────────────────────── */
typedef enum {
    SHELL_TRANSPORT_UART   = 0,
    SHELL_TRANSPORT_TELNET = 1
} shell_transport_t;

/* ────────────────────────────────────────────────
   Session
   ──────────────────────────────────────────────── */
#define SHELL_MAX_SESSIONS  4

typedef struct {
    bool              active;
    shell_transport_t transport;
    int               fd;               /* socket fd for telnet; -1 for UART */
    bool              authenticated;
    char              remote_ip[20];
} shell_session_t;

/* ────────────────────────────────────────────────
   Command Registration
   ──────────────────────────────────────────────── */
typedef int (*shell_cmd_fn_t)(int fd, int argc, char **argv);

typedef struct {
    const char    *name;
    const char    *description;
    const char    *usage;
    shell_cmd_fn_t handler;
} shell_command_t;

/* Return codes from command handlers */
#define SHELL_CMD_OK        0
#define SHELL_CMD_ERROR    -1
#define SHELL_CMD_EXIT    -99   /* for 'exit' command */

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */

/** Initialize shell (register built-in commands) */
esp_err_t shell_init(void);

/** Register a new command.  Returns ESP_ERR_NO_MEM if table full. */
esp_err_t shell_register_command(const shell_command_t *cmd);

/** Start UART shell task */
esp_err_t shell_start_uart(void);

/** Start Telnet server task on given port */
esp_err_t shell_start_telnet(uint16_t port);

/** Find command by name */
const shell_command_t *shell_find_command(const char *name);

/** Print all commands to fd */
void shell_print_help(int fd);

/** Execute a command line string in a session context.
 *  Returns SHELL_CMD_* code. */
int shell_execute(int fd, const char *line);

/** Output helpers (use fd=-1 for UART) */
void shell_write(int fd, const char *str);
void shell_printf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* ────────────────────────────────────────────────
   Shell command registration helpers for modules
   ──────────────────────────────────────────────── */
#define SHELL_CMD_ENTRY(_name, _desc, _usage, _fn) \
    { .name = (_name), .description = (_desc), .usage = (_usage), .handler = (_fn) }

#ifdef __cplusplus
}
#endif
