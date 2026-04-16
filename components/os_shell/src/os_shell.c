#include "os_shell.h"
#include "os_logging.h"
#include "os_env.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "lwip/sockets.h"

#define TAG "SHELL"

/* ────────────────────────────────────────────────
   Command Table
   ──────────────────────────────────────────────── */
static shell_command_t s_cmds[SHELL_MAX_COMMANDS];
static int             s_cmd_count = 0;

/* ────────────────────────────────────────────────
   History Buffer (per-session via stack)
   ──────────────────────────────────────────────── */
typedef struct {
    char    lines[SHELL_HISTORY_DEPTH][SHELL_MAX_LINE_LEN];
    int     head;   /* next write slot */
    int     count;  /* entries stored  */
    int     nav;    /* navigation index (-1 = not navigating) */
} history_t;

static void history_add(history_t *h, const char *line)
{
    if (!line || line[0] == '\0') return;
    /* Don't add duplicate of last entry */
    if (h->count > 0) {
        int last = ((h->head - 1) + SHELL_HISTORY_DEPTH) % SHELL_HISTORY_DEPTH;
        if (strncmp(h->lines[last], line, SHELL_MAX_LINE_LEN) == 0) {
            h->nav = -1;
            return;
        }
    }
    strncpy(h->lines[h->head], line, SHELL_MAX_LINE_LEN - 1);
    h->lines[h->head][SHELL_MAX_LINE_LEN - 1] = '\0';
    h->head = (h->head + 1) % SHELL_HISTORY_DEPTH;
    if (h->count < SHELL_HISTORY_DEPTH) h->count++;
    h->nav = -1;
}

static const char *history_prev(history_t *h)
{
    if (h->count == 0) return NULL;
    if (h->nav < 0) h->nav = h->count - 1;
    else if (h->nav > 0) h->nav--;
    int idx = ((h->head - 1 - h->nav) + SHELL_HISTORY_DEPTH * 2) % SHELL_HISTORY_DEPTH;
    return h->lines[idx];
}

static const char *history_next(history_t *h)
{
    if (h->nav < 0) return NULL;
    if (h->nav == 0) { h->nav = -1; return ""; }
    h->nav--;
    int idx = ((h->head - 1 - h->nav) + SHELL_HISTORY_DEPTH * 2) % SHELL_HISTORY_DEPTH;
    return h->lines[idx];
}

/* ────────────────────────────────────────────────
   I/O Helpers
   ──────────────────────────────────────────────── */
void shell_write(int fd, const char *str)
{
    if (!str) return;
    size_t len = strlen(str);
    if (fd < 0) {
        uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, str, len);
    } else {
        send(fd, str, len, 0);
    }
}

void shell_printf(int fd, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    shell_write(fd, buf);
}

static int shell_getchar(int fd, uint8_t *c, int timeout_ms)
{
    if (fd < 0) {
        /* UART: blocking read with timeout */
        return uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, c, 1,
                               pdMS_TO_TICKS(timeout_ms));
    } else {
        /* Telnet socket */
        struct timeval tv = { .tv_sec = timeout_ms / 1000,
                              .tv_usec = (timeout_ms % 1000) * 1000 };
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r <= 0) return 0;
        return recv(fd, c, 1, 0);
    }
}

static void shell_redraw_line(int fd, const char *buf, int len, int cursor)
{
    /* Move to beginning of line, clear it, rewrite */
    shell_write(fd, "\r" SHELL_PROMPT);
    shell_write(fd, buf);
    /* Position cursor */
    if (cursor < len) {
        shell_printf(fd, "\033[%dD", len - cursor);
    }
}

/* ────────────────────────────────────────────────
   Line Editor
   ──────────────────────────────────────────────── */
typedef enum {
    ES_NONE, ES_ESC, ES_BRACKET
} esc_state_t;

static int shell_readline(int fd, char *out_buf, int buf_sz, history_t *hist)
{
    char        buf[SHELL_MAX_LINE_LEN] = {0};
    int         len    = 0;   /* chars in buf    */
    int         cursor = 0;   /* cursor position */
    esc_state_t es     = ES_NONE;
    uint8_t     c;

    shell_write(fd, SHELL_PROMPT);

    while (1) {
        int n = shell_getchar(fd, &c, 100);
        if (n <= 0) {
            /* On socket, 0 means disconnected */
            if (fd >= 0 && n == 0) return -1;
            continue;
        }

        /* ── Escape sequence state machine ── */
        if (es == ES_ESC) {
            if (c == '[') { es = ES_BRACKET; continue; }
            es = ES_NONE;
        } else if (es == ES_BRACKET) {
            es = ES_NONE;
            switch (c) {
            case 'A': { /* Up: history prev */
                const char *h = history_prev(hist);
                if (h) {
                    /* Clear current content */
                    shell_printf(fd, "\r" SHELL_PROMPT "%*s\r" SHELL_PROMPT,
                                 len, "");
                    strncpy(buf, h, buf_sz - 1);
                    len = cursor = (int)strlen(buf);
                    shell_write(fd, buf);
                }
                break;
            }
            case 'B': { /* Down: history next */
                const char *h = history_next(hist);
                if (h) {
                    shell_printf(fd, "\r" SHELL_PROMPT "%*s\r" SHELL_PROMPT,
                                 len, "");
                    strncpy(buf, h, buf_sz - 1);
                    len = cursor = (int)strlen(buf);
                    shell_write(fd, buf);
                }
                break;
            }
            case 'C': /* Right */
                if (cursor < len) {
                    cursor++;
                    shell_write(fd, "\033[C");
                }
                break;
            case 'D': /* Left */
                if (cursor > 0) {
                    cursor--;
                    shell_write(fd, "\033[D");
                }
                break;
            case 'H': /* Home */
            case '1':
                if (cursor > 0) {
                    shell_printf(fd, "\033[%dD", cursor);
                    cursor = 0;
                }
                break;
            case 'F': /* End */
            case '4':
                if (cursor < len) {
                    shell_printf(fd, "\033[%dC", len - cursor);
                    cursor = len;
                }
                break;
            case '3': /* Delete key: next char is '~' */
                if (cursor < len) {
                    memmove(buf + cursor, buf + cursor + 1, len - cursor - 1);
                    len--;
                    buf[len] = '\0';
                    shell_write(fd, "\033[P");
                    shell_redraw_line(fd, buf, len, cursor);
                }
                break;
            }
            continue;
        }

        /* ── Normal character processing ── */
        switch (c) {
        case 0x1B: /* ESC */
            es = ES_ESC;
            break;

        case '\r':
        case '\n':
            shell_write(fd, "\r\n");
            buf[len] = '\0';
            strncpy(out_buf, buf, buf_sz - 1);
            out_buf[buf_sz - 1] = '\0';
            history_add(hist, buf);
            return len;

        case 0x7F: /* Backspace / DEL */
        case '\b':
            if (cursor > 0 && len > 0) {
                memmove(buf + cursor - 1, buf + cursor, len - cursor);
                cursor--;
                len--;
                buf[len] = '\0';
                shell_write(fd, "\b \b");
                if (cursor < len) {
                    /* Redraw from cursor */
                    shell_write(fd, buf + cursor);
                    shell_printf(fd, " \033[%dD", len - cursor + 1);
                }
            }
            break;

        case 0x03: /* Ctrl+C */
            shell_write(fd, "^C\r\n");
            buf[0] = '\0';
            len = cursor = 0;
            shell_write(fd, SHELL_PROMPT);
            break;

        case 0x04: /* Ctrl+D */
            if (len == 0) return -1; /* EOF */
            break;

        case 0x0C: /* Ctrl+L */
            shell_write(fd, "\033[2J\033[H");
            shell_redraw_line(fd, buf, len, cursor);
            break;

        case 0x15: /* Ctrl+U: clear line */
            if (len > 0) {
                shell_printf(fd, "\r" SHELL_PROMPT "%*s\r" SHELL_PROMPT, len, "");
                memset(buf, 0, sizeof(buf));
                len = cursor = 0;
            }
            break;

        case 0x01: /* Ctrl+A: home */
            if (cursor > 0) {
                shell_printf(fd, "\033[%dD", cursor);
                cursor = 0;
            }
            break;

        case 0x05: /* Ctrl+E: end */
            if (cursor < len) {
                shell_printf(fd, "\033[%dC", len - cursor);
                cursor = len;
            }
            break;

        default:
            if (c >= 0x20 && c < 0x7F && len < buf_sz - 1) {
                /* Insert at cursor */
                if (cursor < len) {
                    memmove(buf + cursor + 1, buf + cursor, len - cursor);
                    buf[cursor] = (char)c;
                    len++;
                    cursor++;
                    /* Echo: write from cursor to end, then reposition */
                    shell_write(fd, buf + cursor - 1);
                    shell_printf(fd, "\033[%dD", len - cursor);
                } else {
                    buf[cursor++] = (char)c;
                    len++;
                    uint8_t echo = c;
                    if (fd < 0) uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &echo, 1);
                    else send(fd, &echo, 1, 0);
                }
            }
            break;
        }
    }
}

/* ────────────────────────────────────────────────
   Command Table
   ──────────────────────────────────────────────── */
esp_err_t shell_register_command(const shell_command_t *cmd)
{
    if (!cmd || !cmd->name || !cmd->handler) return ESP_ERR_INVALID_ARG;
    if (s_cmd_count >= SHELL_MAX_COMMANDS) {
        OS_LOGE(TAG, "Command table full");
        return ESP_ERR_NO_MEM;
    }
    /* Check duplicate */
    for (int i = 0; i < s_cmd_count; i++) {
        if (strcmp(s_cmds[i].name, cmd->name) == 0) {
            s_cmds[i] = *cmd; /* overwrite */
            return ESP_OK;
        }
    }
    s_cmds[s_cmd_count++] = *cmd;
    return ESP_OK;
}

const shell_command_t *shell_find_command(const char *name)
{
    for (int i = 0; i < s_cmd_count; i++) {
        if (strcmp(s_cmds[i].name, name) == 0) return &s_cmds[i];
    }
    return NULL;
}

void shell_print_help(int fd)
{
    shell_write(fd, "\r\n\033[1mAvailable Commands:\033[0m\r\n\r\n");
    for (int i = 0; i < s_cmd_count; i++) {
        shell_printf(fd, "  \033[32m%-18s\033[0m  %s\r\n",
                     s_cmds[i].name,
                     s_cmds[i].description ? s_cmds[i].description : "");
    }
    shell_write(fd, "\r\nType '<cmd> --help' for usage details.\r\n\r\n");
}

/* ────────────────────────────────────────────────
   Command Parser (argc/argv style)
   ──────────────────────────────────────────────── */
static int parse_args(char *line, char **argv, int max_argc)
{
    int argc = 0;
    char *p  = line;

    while (*p && argc < max_argc - 1) {
        /* skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (*p == '"') {
            /* Quoted string */
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* Simple pipe simulation: cmd1 | cmd2 — stdout of first goes to string buffer,
   which becomes stdin of second via injected arg. */
int shell_execute(int fd, const char *raw_line)
{
    if (!raw_line || raw_line[0] == '\0') return SHELL_CMD_OK;

    /* Skip comments */
    if (raw_line[0] == '#') return SHELL_CMD_OK;

    char line[SHELL_MAX_LINE_LEN];
    char expanded[SHELL_MAX_LINE_LEN];
    strncpy(line, raw_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    os_env_expand(line, expanded, sizeof(expanded));
    strncpy(line, expanded, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    /* Trim trailing whitespace */
    int tlen = (int)strlen(line);
    while (tlen > 0 && isspace((unsigned char)line[tlen - 1])) line[--tlen] = '\0';

    char *argv[SHELL_MAX_ARGS];
    int   argc = parse_args(line, argv, SHELL_MAX_ARGS);
    if (argc == 0) return SHELL_CMD_OK;

    /* Check for --help */
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        const shell_command_t *c = shell_find_command(argv[0]);
        if (c) {
            shell_printf(fd, "\r\nUsage: %s\r\n%s\r\n\r\n",
                         c->usage ? c->usage : c->name,
                         c->description ? c->description : "");
        }
        return SHELL_CMD_OK;
    }

    const shell_command_t *cmd = shell_find_command(argv[0]);
    if (!cmd) {
        shell_printf(fd, "Command not found: '%s'  (type 'help')\r\n", argv[0]);
        return SHELL_CMD_ERROR;
    }

    int ret = cmd->handler(fd, argc, argv);
    return ret;
}

/* ────────────────────────────────────────────────
   UART Shell Task
   ──────────────────────────────────────────────── */
static void uart_shell_task(void *arg)
{
    (void)arg;

    /* Configure UART if not already done by IDF console */
    uart_config_t uart_cfg = {
        .baud_rate           = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 512, 512, 0, NULL, 0);
    uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_cfg);

    shell_write(-1, SHELL_BANNER);

    history_t *hist = calloc(1, sizeof(*hist));
    if (!hist) {
        OS_LOGE(TAG, "Failed to allocate shell history");
        vTaskDelete(NULL);
        return;
    }
    hist->nav = -1;
    char line[SHELL_MAX_LINE_LEN];

    while (1) {
        int n = shell_readline(-1, line, sizeof(line), hist);
        if (n < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (n > 0) {
            shell_execute(-1, line);
        }
    }

    free(hist);
}

/* ────────────────────────────────────────────────
   Telnet Session Handler
   ──────────────────────────────────────────────── */
#define TELNET_PORT_DEFAULT  2222
#define TELNET_MAX_SESSIONS  3
#define TELNET_STACK_SIZE    8192
#define TELNET_PRIORITY      6
#define UART_SHELL_STACK_SIZE 16384

/* Credentials (change via NVS in production) */
#define TELNET_USERNAME      "admin"
#define TELNET_PASSWORD      "esp32os"

typedef struct { int fd; char ip[20]; } telnet_session_arg_t;

static void telnet_session_task(void *arg)
{
    telnet_session_arg_t sa_local;
    if (arg) {
        telnet_session_arg_t *sa = arg;
        memcpy(&sa_local, sa, sizeof(sa_local));
        free(sa);
    }
    
    int fd = sa_local.fd;
    char remote_ip[20];
    strncpy(remote_ip, sa_local.ip, sizeof(remote_ip) - 1);

    OS_LOGI(TAG, "Telnet session started from %s", remote_ip);

    history_t *hist = calloc(1, sizeof(*hist));
    if (!hist) {
        OS_LOGE(TAG, "Failed to allocate telnet history");
        close(fd);
        vTaskDelete(NULL);
        return;
    }
    hist->nav = -1;

    /* Set socket timeout */
    struct timeval tv = { .tv_sec = 300, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Auth challenge */
    char user[48] = {0}, pass[48] = {0};
    shell_write(fd, "\r\n\033[1mesp32os login:\033[0m ");
    int n;

    n = shell_readline(fd, user, sizeof(user), hist);
    if (n < 0) goto done;

    shell_write(fd, "Password: ");
    n = shell_readline(fd, pass, sizeof(pass), hist);
    if (n < 0) goto done;

    if (strcmp(user, TELNET_USERNAME) != 0 || strcmp(pass, TELNET_PASSWORD) != 0) {
        shell_write(fd, "\r\nLogin incorrect.\r\n");
        OS_LOGW(TAG, "Auth failed from %s (user='%s')", remote_ip, user);
        goto done;
    }
    OS_LOGI(TAG, "Auth OK from %s (user='%s')", remote_ip, user);
    shell_write(fd, SHELL_BANNER);

    char line[SHELL_MAX_LINE_LEN];

    while (1) {
        n = shell_readline(fd, line, sizeof(line), hist);
        if (n < 0) break;  /* disconnected */
        if (n > 0) {
            int ret = shell_execute(fd, line);
            if (ret == SHELL_CMD_EXIT) break;
        }
    }

done:
    OS_LOGI(TAG, "Telnet session closed (%s)", remote_ip);
    free(hist);
    close(fd);
    vTaskDelete(NULL);
}

static void telnet_server_task(void *arg)
{
    uint16_t port = (uint16_t)(uintptr_t)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        OS_LOGE(TAG, "Telnet socket create failed");
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(server_fd, TELNET_MAX_SESSIONS) != 0) {
        OS_LOGE(TAG, "Telnet bind/listen failed");
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    OS_LOGI(TAG, "Telnet server listening on port %d", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        char client_ip[20];
        inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));
        OS_LOGI(TAG, "Telnet connection from %s", client_ip);

        telnet_session_arg_t *sa = malloc(sizeof(*sa));
        if (!sa) { close(client_fd); continue; }
        sa->fd = client_fd;
        strncpy(sa->ip, client_ip, sizeof(sa->ip) - 1);
        sa->ip[sizeof(sa->ip) - 1] = '\0';

        char task_name[24];
        snprintf(task_name, sizeof(task_name), "telnet_%.15s", client_ip);
        xTaskCreate(telnet_session_task, task_name,
                    TELNET_STACK_SIZE, sa, TELNET_PRIORITY, NULL);
    }
}

/* ────────────────────────────────────────────────
   Init
   ──────────────────────────────────────────────── */
esp_err_t shell_init(void)
{
    OS_LOGI(TAG, "Shell init  (cmds max=%d)", SHELL_MAX_COMMANDS);
    return ESP_OK;
}

esp_err_t shell_start_uart(void)
{
    BaseType_t rc = xTaskCreate(uart_shell_task, "uart_shell",
                                UART_SHELL_STACK_SIZE, NULL, 5, NULL);
    return (rc == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t shell_start_telnet(uint16_t port)
{
    if (port == 0) port = TELNET_PORT_DEFAULT;
    BaseType_t rc = xTaskCreate(telnet_server_task, "telnet_srv",
                                4096, (void *)(uintptr_t)port,
                                TELNET_PRIORITY, NULL);
    return (rc == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
