#pragma once
#include "esp_err.h"
typedef struct esp_console_repl {
    esp_err_t (*del)(struct esp_console_repl *repl);
} esp_console_repl_t;
typedef struct { const char *prompt; int max_cmdline_length; } esp_console_repl_config_t;
typedef struct { int unused; } esp_console_dev_uart_config_t;
typedef struct {
    const char *command;
    const char *help;
    int (*func)(int argc, char **argv);
} esp_console_cmd_t;
#define ESP_CONSOLE_REPL_CONFIG_DEFAULT() ((esp_console_repl_config_t){0})
#define ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT() ((esp_console_dev_uart_config_t){0})
esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *uart,
                                  const esp_console_repl_config_t *config,
                                  esp_console_repl_t **repl);
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *command);
esp_err_t esp_console_register_help_command(void);
esp_err_t esp_console_start_repl(esp_console_repl_t *repl);
