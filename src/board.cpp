#include "board.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

namespace {
constexpr const char *kTag = "board";
}

i2c_master_bus_handle_t g_i2c_bus = nullptr;

esp_err_t board_i2c_add(uint8_t address, i2c_master_dev_handle_t *out) {
  if (g_i2c_bus == nullptr) {
    ESP_LOGE(kTag, "I2C bus not initialised");
    return ESP_ERR_INVALID_STATE;
  }
  const i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = address,
      .scl_speed_hz = 400000,
  };
  return i2c_master_bus_add_device(g_i2c_bus, &cfg, out);
}
