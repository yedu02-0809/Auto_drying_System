#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct mock_bus *i2c_master_bus_handle_t;
typedef struct mock_device *i2c_master_dev_handle_t;
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
typedef struct {
    int i2c_port;
    int sda_io_num;
    int scl_io_num;
    int clk_source;
    int glitch_ignore_cnt;
    struct { bool enable_internal_pullup; } flags;
} i2c_master_bus_config_t;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config,
                             i2c_master_bus_handle_t *bus);
