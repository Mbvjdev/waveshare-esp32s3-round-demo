#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// Shared I2C master bus for the board's on-chip peripherals (TCA9554, QMI8658).
// Populated by main.cpp before either consumer is used.
extern i2c_master_bus_handle_t g_i2c_bus;

// Add one I2C device to the shared bus at the given address.
esp_err_t board_i2c_add(uint8_t address, i2c_master_dev_handle_t *out);
